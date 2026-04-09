const express = require('express');
const http = require('http');
const { Server } = require('socket.io');
const mqtt = require('mqtt');
const fs = require('fs');
const path = require('path');
const cors = require('cors');

// Charge les variables d'environnement
require('dotenv').config();

const app = express();
const server = http.createServer(app);

// Configuration Socket.io (autorise les connexions locales)
const io = new Server(server, {
  cors: { origin: '*' }
});

// Middleware
app.use(cors());
app.use(express.static(path.join(__dirname, '..'))); // Sert les fichiers HTML statiques depuis le dossier parent

// === CONFIGURATION ===
const SUPER_ADMIN_PIN = process.env.SUPER_ADMIN_PIN || '7423';
const SUPER_ADMIN_NAME = 'Admin Principal';
const CODE_EXPIRY_MS = 12 * 60 * 60 * 1000; // 12h
const DB_FILE = path.join(__dirname, 'database.json');

// === MQTT CONFIGURATION ===
const mqttOptions = {
  host: process.env.MQTT_HOST || 'broker.hivemq.com',
  port: process.env.MQTT_PORT || 8884,
  protocol: process.env.MQTT_PROTOCOL || 'wss',
  clientId: 'server-porte-' + Math.random().toString(36).substring(2, 8),
  clean: true,
};

// Topics MQTT
const TOPICS = {
  cmd: 'porte-epcrd-2026/commande',
  state: 'porte-epcrd-2026/etat',
  alarme: 'porte-epcrd-2026/alarme',
  // Ces topics ne sont conservés que pour la rétrocompatibilité (esp32 ou autre client non mis à jour)
  // L'idéal est que la porte n'écoute que "cmd" et réponde sur "state".
  codeResult: 'porte-epcrd-2026/coderesult',
  codeUsed: 'porte-epcrd-2026/codeused'
};

// === STATE & DATABASE ===
let db = {
  admins: [],
  history: [],
  doorState: 'OFF'
};

// Charge la base de données au démarrage
function loadDB() {
  if (fs.existsSync(DB_FILE)) {
    try {
      const data = fs.readFileSync(DB_FILE, 'utf8');
      db = JSON.parse(data);
      console.log('Database loaded.');
    } catch (e) {
      console.error('Error loading DB:', e);
    }
  } else {
    saveDB(); // Crée le fichier initial
  }
}

// Sauvegarde la base de données
function saveDB() {
  fs.writeFileSync(DB_FILE, JSON.stringify(db, null, 2), 'utf8');
}

loadDB();

// === MQTT CLIENT ===
const mqttClient = mqtt.connect(mqttOptions);

mqttClient.on('connect', () => {
  console.log('Connecté au broker MQTT');
  mqttClient.subscribe(TOPICS.state);
  mqttClient.subscribe(TOPICS.codeUsed); // Si l'ESP32 publie encore dessus
});

mqttClient.on('message', (topic, message) => {
  const msg = message.toString();
  
  if (topic === TOPICS.state) {
    db.doorState = msg;
    io.emit('stateUpdate', msg); // Broadcast à tous les clients connectés
  } else if (topic === TOPICS.codeUsed) {
    // Si l'ESP32 nous informe qu'un code a été utilisé (rétrocompatibilité)
    markCodeUsed(msg);
  }
});

mqttClient.on('error', (err) => {
  console.error('Erreur MQTT:', err);
});

// === LOGIQUE METIER ===

function markCodeUsed(code) {
  const entry = db.history.find(c => c.code === code);
  const now = new Date();
  const timeStr = now.getHours().toString().padStart(2, '0') + ':' + now.getMinutes().toString().padStart(2, '0');
  
  if (entry) {
    entry.status = 'used';
    entry.usedAt = timeStr;
  } else {
    db.history.unshift({ code, status: 'used', time: '??:??', usedAt: timeStr, from: 'inconnu', createdAt: Date.now() });
  }
  saveDB();
  io.emit('historyUpdate', db.history); // Met à jour tous les clients
}

// Nettoyage de l'historique des codes expirés (optionnel, pour garder la DB propre)
setInterval(() => {
  let changed = false;
  db.history.forEach(c => {
    if (c.status === 'waiting' && (Date.now() > c.createdAt + CODE_EXPIRY_MS)) {
        // Optionnel : on pourrait le marquer 'expired', ici on le laisse tel quel
        // car le client fait le check, mais on pourrait nettoyer.
    }
  });
}, 60000);

// === WEBSOCKETS (SOCKET.IO) ===

io.on('connection', (socket) => {
  console.log('Nouveau client connecté:', socket.id);
  
  // Par défaut, le socket n'est pas authentifié
  socket.isAdmin = false;
  socket.userName = 'Guest';
  socket.isSuperAdmin = false;

  // Envoi de l'état initial (publique)
  socket.emit('stateUpdate', db.doorState);

  // --- Authentification Admin ---
  socket.on('login', (pin) => {
    if (pin === SUPER_ADMIN_PIN) {
      socket.isAdmin = true;
      socket.isSuperAdmin = true;
      socket.userName = SUPER_ADMIN_NAME;
      
      socket.emit('loginSuccess', { name: socket.userName, role: 'Admin Principal', isSuperAdmin: true });
      socket.emit('historyUpdate', db.history);
      socket.emit('adminsUpdate', db.admins);
      return;
    }

    const admin = db.admins.find(a => a.pin === pin);
    if (admin) {
      const now = Date.now();
      if (admin.startsAt && now < admin.startsAt) {
        socket.emit('loginError', 'Accès actif à partir du ' + new Date(admin.startsAt).toLocaleDateString());
        return;
      }
      if (admin.expiresAt && now > admin.expiresAt) {
        socket.emit('loginError', 'Accès expiré pour ' + admin.name);
        return;
      }
      socket.isAdmin = true;
      socket.isSuperAdmin = false;
      socket.userName = admin.name;
      socket.emit('loginSuccess', { name: socket.userName, role: 'Hôte', isSuperAdmin: false });
      socket.emit('historyUpdate', db.history);
      socket.emit('adminsUpdate', db.admins);
      return;
    }

    socket.emit('loginError', 'Code incorrect');
  });

  // --- Commandes Admin (Protégées) ---
  socket.on('command', (cmd) => {
    if (!socket.isAdmin) return;
    if (cmd === 'ON' || cmd === 'OFF') {
      mqttClient.publish(TOPICS.cmd, cmd);
    }
  });

  socket.on('alarme', (state) => {
     if (!socket.isAdmin) return;
     if (state === 'ON' || state === 'OFF') {
       mqttClient.publish(TOPICS.alarme, state);
     }
  });

  // --- Gestion des Codes Jetables (Admin) ---
  socket.on('generateCode', () => {
    if (!socket.isAdmin) return;
    const code = String(Math.floor(100000 + Math.random() * 900000));
    const now = new Date();
    const time = now.getHours().toString().padStart(2,'0') + ':' + now.getMinutes().toString().padStart(2,'0');
    
    const newEntry = { code, status: 'waiting', time, from: socket.userName, createdAt: Date.now() };
    db.history.unshift(newEntry);
    saveDB();
    
    // Notifie le créateur du code généré
    socket.emit('codeGenerated', code);
    // Met à jour tous les admins
    io.emit('historyUpdate', db.history);
  });

  socket.on('deleteCode', (code) => {
    if (!socket.isAdmin) return;
    db.history = db.history.filter(c => c.code !== code);
    saveDB();
    io.emit('historyUpdate', db.history);
  });

  socket.on('clearHistory', () => {
     if (!socket.isAdmin) return;
     db.history = [];
     saveDB();
     io.emit('historyUpdate', db.history);
  });

  // --- Vérification Code Invité (Publique) ---
  socket.on('verifyGuestCode', (code) => {
    const entry = db.history.find(c => c.code === code && c.status === 'waiting');
    
    if (entry) {
      if (Date.now() > entry.createdAt + CODE_EXPIRY_MS) {
        socket.emit('guestResult', { success: false, message: 'Ce code a expiré' });
        return;
      }
      // Code valide ! On ouvre la porte.
      mqttClient.publish(TOPICS.cmd, 'ON');
      markCodeUsed(code);
      socket.emit('guestResult', { success: true, message: 'Code valide. Porte déverrouillée (5s)' });
      
      // Auto-fermeture après 5s (optionnel si géré par l'ESP32, mais bon en backup)
      setTimeout(() => {
        mqttClient.publish(TOPICS.cmd, 'OFF');
      }, 5000);

    } else {
      socket.emit('guestResult', { success: false, message: 'Code invalide ou déjà utilisé' });
    }
  });

  // --- Gestion des Admins (Super Admin Only) ---
  socket.on('addAdmin', (adminData) => {
    if (!socket.isSuperAdmin) return;
    // adminData : { name, pin, startsAt, expiresAt }
    
    if (db.admins.find(a => a.pin === adminData.pin) || adminData.pin === SUPER_ADMIN_PIN) {
        socket.emit('adminError', 'Ce PIN est déjà utilisé');
        return;
    }

    db.admins.push(adminData);
    saveDB();
    io.emit('adminsUpdate', db.admins);
    socket.emit('adminSuccess', 'Hôte ajouté : ' + adminData.name);
  });

  socket.on('deleteAdmin', (pin) => {
    if (!socket.isSuperAdmin) return;
    db.admins = db.admins.filter(a => a.pin !== pin);
    saveDB();
    io.emit('adminsUpdate', db.admins);
  });

  socket.on('disconnect', () => {
    console.log('Client déconnecté:', socket.id);
  });
});

const PORT = process.env.PORT || 3000;
server.listen(PORT, () => {
  console.log(`Serveur démarré sur http://localhost:${PORT}`);
});