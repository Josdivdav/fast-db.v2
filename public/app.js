// FastDB Developer Console & Data Studio
let socket;
let currentUser = null;

// Active Studio State
let activeStudioDb = null;
let activeCollection = null;
let activeDocumentId = null;
let studioCollections = [];
let studioDocuments = [];
let studioViewMode = 'columns'; // 'columns' | 'table'

// Initialize Socket.io connection
function initSocket() {
    socket = io({ transports: ['websocket', 'polling'] });

    const pill = document.getElementById('connection-pill');
    const text = document.getElementById('connection-status-text');

    socket.on('connect', () => {
        pill.className = 'status-pill connected';
        text.textContent = `Connected to FastDB (${socket.id.substring(0, 6)}…)`;
    });

    socket.on('disconnect', () => {
        pill.className = 'status-pill disconnected';
        text.textContent = 'Disconnected from FastDB';
    });

    socket.on('connect_error', () => {
        pill.className = 'status-pill disconnected';
        text.textContent = 'Connection Error — Retrying...';
    });
}

// Socket Request Helper with correlation
function emitRequest(event, data = {}) {
    return new Promise((resolve, reject) => {
        const _reqId = Math.random().toString(36).substring(2, 10) + Date.now().toString(36);
        const payload = { ...data, _reqId };

        const onEvent = (response) => {
            if (response && response._reqId && response._reqId !== _reqId) {
                return;
            }
            socket.off(event, onEvent);
            if (response && response.error) {
                const err = new Error(typeof response.error === 'string' ? response.error : JSON.stringify(response.error));
                reject(err);
                return;
            }
            resolve(response);
        };

        socket.on(event, onEvent);
        socket.emit(event, payload);
    });
}

// Toast Notifications
function showToast(message, isError = false) {
    const container = document.getElementById('toast-container');
    const toast = document.createElement('div');
    toast.className = `toast ${isError ? 'error' : ''}`;
    toast.innerHTML = `
        <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="${isError ? '#F43F5E' : '#10B981'}" stroke-width="2">
            ${isError 
                ? '<circle cx="12" cy="12" r="10"></circle><line x1="12" y1="8" x2="12" y2="12"></line><line x1="12" y1="16" x2="12.01" y2="16"></line>' 
                : '<path d="M22 11.08V12a10 10 0 1 1-5.93-9.14"></path><polyline points="22 4 12 14.01 9 11.01"></polyline>'
            }
        </svg>
        <span>${message}</span>
    `;
    container.appendChild(toast);
    setTimeout(() => {
        toast.style.opacity = '0';
        toast.style.transform = 'translateY(10px)';
        toast.style.transition = 'all 0.2s';
        setTimeout(() => toast.remove(), 200);
    }, 2800);
}

// Copy to Clipboard Utility
async function copyText(text, label = 'Copied') {
    try {
        await navigator.clipboard.writeText(text);
        showToast(`Copied ${label} to clipboard!`);
    } catch {
        const textarea = document.createElement('textarea');
        textarea.value = text;
        document.body.appendChild(textarea);
        textarea.select();
        document.execCommand('copy');
        textarea.remove();
        showToast(`Copied ${label} to clipboard!`);
    }
}

// Switch between Register and Login tabs
function switchAuthTab(tab) {
    const regBtn = document.getElementById('tab-register-btn');
    const loginBtn = document.getElementById('tab-login-btn');
    const regForm = document.getElementById('register-form');
    const loginForm = document.getElementById('login-form');

    if (tab === 'register') {
        regBtn.classList.add('active');
        loginBtn.classList.remove('active');
        regForm.classList.remove('hidden');
        loginForm.classList.add('hidden');
    } else {
        loginBtn.classList.add('active');
        regBtn.classList.remove('active');
        loginForm.classList.remove('hidden');
        regForm.classList.add('hidden');
    }
}

// Handle Register
async function handleRegister(e) {
    e.preventDefault();
    const btn = document.getElementById('reg-submit-btn');
    const name = document.getElementById('reg-name').value.trim();
    const email = document.getElementById('reg-email').value.trim();
    const password = document.getElementById('reg-password').value;

    btn.disabled = true;
    btn.innerHTML = '<span>Creating Account...</span>';

    try {
        const res = await emitRequest('user:register', { name, email, password });
        
        // Robust UID and user object resolution
        let uid = res.uid || (res.user && res.user.uid);
        if (!uid) {
            uid = Object.keys(res).find(k => k !== '_reqId' && k !== 'user' && k !== 'error');
        }
        const userObj = res.user || (uid && res[uid]) || {};

        currentUser = {
            uid: uid || userObj.uid || 'dev-session',
            name: userObj.name || name,
            email: userObj.email || email,
            token: userObj.token || res.token || ''
        };

        localStorage.setItem('fastdb_user', JSON.stringify(currentUser));
        showToast(`Account created for ${currentUser.name}!`);
        renderLoggedInState();
    } catch (err) {
        showToast(err.message || 'Registration failed', true);
    } finally {
        btn.disabled = false;
        btn.innerHTML = '<span>Create Developer Account</span>';
    }
}

// Handle Login
async function handleLogin(e) {
    e.preventDefault();
    const btn = document.getElementById('login-submit-btn');
    const email = document.getElementById('login-email').value.trim();
    const password = document.getElementById('login-password').value;

    btn.disabled = true;
    btn.innerHTML = '<span>Signing In...</span>';

    try {
        const res = await emitRequest('user:login', { email, password });
        const userObj = res.user || {};
        currentUser = {
            uid: userObj.uid || res.uid || 'dev-session',
            name: userObj.name || 'Developer',
            email: userObj.email || email,
            token: res.token || userObj.token || ''
        };

        localStorage.setItem('fastdb_user', JSON.stringify(currentUser));
        showToast(`Welcome back, ${currentUser.name}!`);
        renderLoggedInState();
    } catch (err) {
        showToast(err.message || 'Login failed', true);
    } finally {
        btn.disabled = false;
        btn.innerHTML = '<span>Sign In to Console</span>';
    }
}

// Handle Logout
function handleLogout() {
    currentUser = null;
    activeStudioDb = null;
    localStorage.removeItem('fastdb_user');
    showToast('Signed out of FastDB');
    renderLoggedOutState();
}

// Render Logged Out View
function renderLoggedOutState() {
    document.getElementById('auth-section').classList.remove('hidden');
    document.getElementById('dashboard-section').classList.add('hidden');
    document.getElementById('nav-user-section').innerHTML = '';
}

// Render Logged In View
function renderLoggedInState() {
    if (!currentUser) return;

    document.getElementById('auth-section').classList.add('hidden');
    document.getElementById('dashboard-section').classList.remove('hidden');

    document.getElementById('dev-name').textContent = currentUser.name || 'Developer';
    document.getElementById('dev-email').textContent = currentUser.email || '';
    document.getElementById('dev-uid').textContent = currentUser.uid || '';
    document.getElementById('dev-avatar').textContent = (currentUser.name || 'D').charAt(0).toUpperCase();

    const navUser = document.getElementById('nav-user-section');
    navUser.innerHTML = `
        <div class="nav-user-info">
            <div class="nav-user-pill">
                <div class="user-mini-avatar">${(currentUser.name || 'D').charAt(0).toUpperCase()}</div>
                <span>${currentUser.name || 'Developer'}</span>
            </div>
            <button class="btn btn-sm btn-ghost" onclick="handleLogout()">Sign Out</button>
        </div>
    `;

    closeDataStudio();
    loadDatabases();
}

// Load Databases from Server
async function loadDatabases() {
    if (!currentUser) return;
    const grid = document.getElementById('databases-grid');
    const empty = document.getElementById('empty-state');

    grid.innerHTML = '<div style="color: var(--text-dim); padding: 1.5rem;">Loading your databases...</div>';

    try {
        const res = await emitRequest('database:list', { userId: currentUser.uid });
        const databases = res.databases || [];

        if (databases.length === 0) {
            grid.innerHTML = '';
            empty.classList.remove('hidden');
            return;
        }

        empty.classList.add('hidden');
        grid.innerHTML = '';

        databases.forEach((db) => {
            const card = createDatabaseCard(db);
            grid.appendChild(card);
        });
    } catch (err) {
        grid.innerHTML = `<div style="color: #FDA4AF; padding: 1.5rem;">Failed to load databases: ${err.message}</div>`;
        showToast(err.message, true);
    }
}

// Create Card Element for Database
function createDatabaseCard(db) {
    const card = document.createElement('div');
    card.className = 'db-card';
    card.id = `db-card-${db.databaseId}`;

    const host = window.location.origin;
    const dateFormatted = db.createdAt ? new Date(db.createdAt).toLocaleDateString(undefined, {
        year: 'numeric', month: 'short', day: 'numeric', hour: '2-digit', minute: '2-digit'
    }) : 'Active';

    const apiKeyMasked = '••••••••••••••••••••••••••••••••••••';

    card.innerHTML = `
        <div class="db-card-header">
            <div>
                <div class="db-title-row">
                    <span class="db-title">${db.name || 'Database'}</span>
                    <span class="badge badge-success">Online</span>
                </div>
                <div class="db-meta">Created ${dateFormatted}</div>
            </div>
            <div class="db-header-actions">
                <button class="btn btn-sm btn-primary" onclick="openDataStudio('${db.databaseId}', '${escapeHtml(db.name)}', '${db.apiKey}', '${db.createdBy || currentUser.uid}')">
                    <svg width="13" height="13" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
                        <path d="M13 2L3 14H12L11 22L21 10H12L13 2Z" fill="currentColor"/>
                    </svg>
                    Grid View
                </button>
                <button class="btn btn-sm btn-danger" onclick="handleDeleteDatabase('${db.databaseId}', '${escapeHtml(db.name)}')" title="Delete Database">
                    <svg width="13" height="13" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
                        <polyline points="3 6 5 6 21 6"></polyline>
                        <path d="M19 6v14a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V6m3 0V4a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v2"></path>
                    </svg>
                </button>
            </div>
        </div>

        <!-- Connection Keys Section -->
        <div class="conn-keys-panel">
            <div class="conn-key-row">
                <span class="conn-key-label">Database ID</span>
                <span class="conn-key-value" id="val-dbid-${db.databaseId}">${db.databaseId}</span>
                <div class="conn-key-actions">
                    <button class="btn btn-sm btn-ghost" onclick="copyText('${db.databaseId}', 'Database ID')">Copy</button>
                </div>
            </div>

            <div class="conn-key-row">
                <span class="conn-key-label">API Key</span>
                <span class="conn-key-value" id="val-key-${db.databaseId}" data-real="${db.apiKey}" data-masked="true">${apiKeyMasked}</span>
                <div class="conn-key-actions">
                    <button class="btn btn-sm btn-ghost" onclick="toggleKeyVisibility('${db.databaseId}')" id="btn-toggle-${db.databaseId}">Show</button>
                    <button class="btn btn-sm btn-ghost" onclick="copyText('${db.apiKey}', 'API Key')">Copy</button>
                </div>
            </div>

            <div class="conn-key-row">
                <span class="conn-key-label">Author UID</span>
                <span class="conn-key-value">${db.createdBy || currentUser.uid}</span>
                <div class="conn-key-actions">
                    <button class="btn btn-sm btn-ghost" onclick="copyText('${db.createdBy || currentUser.uid}', 'Author UID')">Copy</button>
                </div>
            </div>

            <div class="conn-key-row">
                <span class="conn-key-label">Host URL</span>
                <span class="conn-key-value">${host}</span>
                <div class="conn-key-actions">
                    <button class="btn btn-sm btn-ghost" onclick="copyText('${host}', 'Host URL')">Copy</button>
                </div>
            </div>
        </div>

        <!-- Code Snippet Box -->
        <div class="snippet-panel">
            <div class="snippet-header">
                <div class="snippet-tabs">
                    <button class="snippet-tab-btn active" id="tab-sdk-${db.databaseId}" onclick="switchSnippetTab('${db.databaseId}', 'sdk')">Client SDK</button>
                    <button class="snippet-tab-btn" id="tab-env-${db.databaseId}" onclick="switchSnippetTab('${db.databaseId}', 'env')">.env</button>
                    <button class="snippet-tab-btn" id="tab-socket-${db.databaseId}" onclick="switchSnippetTab('${db.databaseId}', 'socket')">Raw Socket</button>
                </div>
                <button class="btn btn-sm btn-ghost" onclick="copySnippetCode('${db.databaseId}')">Copy Snippet</button>
            </div>
            <div class="snippet-code-box">
                <pre class="snippet-code" id="snippet-code-${db.databaseId}">${getSdkSnippet(db)}</pre>
            </div>
        </div>
    `;

    return card;
}

// Toggle Reveal/Hide API Key
function toggleKeyVisibility(dbId) {
    const el = document.getElementById(`val-key-${dbId}`);
    const btn = document.getElementById(`btn-toggle-${dbId}`);
    const isMasked = el.getAttribute('data-masked') === 'true';

    if (isMasked) {
        el.textContent = el.getAttribute('data-real');
        el.setAttribute('data-masked', 'false');
        btn.textContent = 'Hide';
    } else {
        el.textContent = '••••••••••••••••••••••••••••••••••••';
        el.setAttribute('data-masked', 'true');
        btn.textContent = 'Show';
    }
}

// Snippet Generators
function getSdkSnippet(db) {
    const author = db.createdBy || currentUser.uid;
    return `import { Database } from "./socket.helper.js";

const db = new Database({
  databaseId: "${db.databaseId}",
  userEmail: "${currentUser.email}",
  author: "${author}",
  apiKey: "${db.apiKey}"
});

await db.connect();

// Insert document
await db.collection("users").add({
  name: "Alex",
  email: "alex@example.com"
});

// Query documents
const users = await db.collection("users").get();
console.log(users);`;
}

function getEnvSnippet(db) {
    const author = db.createdBy || currentUser.uid;
    return `FASTDB_DATABASE_ID=${db.databaseId}
FASTDB_API_KEY=${db.apiKey}
FASTDB_AUTHOR_ID=${author}
FASTDB_USER_EMAIL=${currentUser.email}
FASTDB_HOST=${window.location.origin}`;
}

function getSocketSnippet(db) {
    const author = db.createdBy || currentUser.uid;
    return `import { io } from "socket.io-client";
const socket = io("${window.location.origin}");

socket.emit("database:connect", {
  databaseId: "${db.databaseId}",
  userEmail: "${currentUser.email}",
  author: "${author}",
  apiKey: "${db.apiKey}"
});

socket.on("database:connect", (res) => {
  console.log("Connected to FastDB:", res);
});`;
}

function switchSnippetTab(dbId, tabType) {
    const btnSdk = document.getElementById(`tab-sdk-${dbId}`);
    const btnEnv = document.getElementById(`tab-env-${dbId}`);
    const btnSocket = document.getElementById(`tab-socket-${dbId}`);
    const codeEl = document.getElementById(`snippet-code-${dbId}`);

    btnSdk.classList.remove('active');
    btnEnv.classList.remove('active');
    btnSocket.classList.remove('active');

    const apiKey = document.getElementById(`val-key-${dbId}`).getAttribute('data-real');
    const mockDb = { databaseId: dbId, apiKey, createdBy: currentUser.uid };

    if (tabType === 'sdk') {
        btnSdk.classList.add('active');
        codeEl.textContent = getSdkSnippet(mockDb);
    } else if (tabType === 'env') {
        btnEnv.classList.add('active');
        codeEl.textContent = getEnvSnippet(mockDb);
    } else if (tabType === 'socket') {
        btnSocket.classList.add('active');
        codeEl.textContent = getSocketSnippet(mockDb);
    }
}

function copySnippetCode(dbId) {
    const code = document.getElementById(`snippet-code-${dbId}`).textContent;
    copyText(code, 'Code Snippet');
}

// ============================================================
// DATA STUDIO IMPLEMENTATION
// ============================================================

// Open Data Studio for a given database
async function openDataStudio(databaseId, name, apiKey, author) {
    activeStudioDb = { databaseId, name, apiKey, author };
    activeCollection = null;
    activeDocumentId = null;

    document.getElementById('databases-overview-view').classList.add('hidden');
    document.getElementById('data-studio-view').classList.remove('hidden');

    document.getElementById('studio-db-name').textContent = name;
    document.getElementById('studio-db-id').textContent = databaseId;

    await refreshStudioData();
}

// Close Data Studio & Return to Databases
function closeDataStudio() {
    activeStudioDb = null;
    document.getElementById('data-studio-view').classList.add('hidden');
    document.getElementById('databases-overview-view').classList.remove('hidden');
}

// Set View Mode: 'columns' (3-Column) or 'table' (Table Grid)
function setStudioViewMode(mode) {
    studioViewMode = mode;
    const btnCols = document.getElementById('btn-view-columns');
    const btnTable = document.getElementById('btn-view-table');
    const containerCols = document.getElementById('studio-columns-container');
    const containerTable = document.getElementById('studio-table-container');

    if (mode === 'columns') {
        btnCols.classList.add('active');
        btnTable.classList.remove('active');
        containerCols.classList.remove('hidden');
        containerTable.classList.add('hidden');
        renderColumnsView();
    } else {
        btnTable.classList.add('active');
        btnCols.classList.remove('active');
        containerCols.classList.add('hidden');
        containerTable.classList.remove('hidden');
        renderTableView();
    }
}

// Refresh all collections and documents
async function refreshStudioData() {
    if (!activeStudioDb) return;

    try {
        const res = await emitRequest('collection:list', {
            databaseId: activeStudioDb.databaseId,
            author: activeStudioDb.author,
            apiKey: activeStudioDb.apiKey
        });
        studioCollections = res.collections || [];

        // If activeCollection is not set or not in list, select first or default
        if (studioCollections.length > 0) {
            if (!activeCollection || !studioCollections.some(c => c.name === activeCollection)) {
                activeCollection = studioCollections[0].name;
            }
        } else {
            activeCollection = null;
        }

        renderCollectionsList();

        if (activeCollection) {
            await selectCollection(activeCollection);
        } else {
            renderDocumentsList([]);
            renderDocumentInspector(null);
            renderTableView();
        }
    } catch (err) {
        showToast(err.message || 'Failed to load collections', true);
    }
}

// Render Collections List (Column 1)
function renderCollectionsList() {
    const listEl = document.getElementById('fs-collections-list');
    listEl.innerHTML = '';

    if (studioCollections.length === 0) {
        listEl.innerHTML = `<div class="fs-empty-hint">No collections yet.<br><br>Click "+ Add Collection" above to start one.</div>`;
        return;
    }

    studioCollections.forEach(col => {
        const item = document.createElement('div');
        item.className = `collection-item ${col.name === activeCollection ? 'active' : ''}`;
        item.onclick = () => selectCollection(col.name);
        item.innerHTML = `
            <div class="collection-item-left">
                <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
                    <path d="M22 19a2 2 0 0 1-2 2H4a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h5l2 3h9a2 2 0 0 1 2 2z"></path>
                </svg>
                <span>${col.name}</span>
            </div>
            <div style="display: flex; align-items: center; gap: 0.35rem;">
                <span class="badge ${col.name === activeCollection ? 'badge-success' : 'badge-cyan'}">${col.count}</span>
                <button class="btn btn-xs btn-ghost" onclick="event.stopPropagation(); handleDeleteCollection('${col.name}')" title="Delete Collection">
                    &times;
                </button>
            </div>
        `;
        listEl.appendChild(item);
    });
}

// Select a Collection
async function selectCollection(colName) {
    activeCollection = colName;
    document.getElementById('fs-active-collection-title').textContent = `${colName}`;
    renderCollectionsList();

    try {
        const res = await emitRequest('document:read', {
            databaseId: activeStudioDb.databaseId,
            author: activeStudioDb.author,
            apiKey: activeStudioDb.apiKey,
            collection: colName
        });
        studioDocuments = res.data || [];

        renderDocumentsList(studioDocuments);

        // Select first document by default if exists
        if (studioDocuments.length > 0) {
            selectDocument(studioDocuments[0]._id);
        } else {
            renderDocumentInspector(null);
        }

        if (studioViewMode === 'table') {
            renderTableView();
        }
    } catch (err) {
        showToast(err.message || 'Failed to read documents', true);
    }
}

// Filter Documents by ID input
function filterDocumentsList(searchTerm) {
    const term = searchTerm.toLowerCase().trim();
    if (!term) {
        renderDocumentsList(studioDocuments);
        return;
    }
    const filtered = studioDocuments.filter(doc => (doc._id || '').toLowerCase().includes(term));
    renderDocumentsList(filtered);
}

// Render Documents List (Column 2)
function renderDocumentsList(docs) {
    const listEl = document.getElementById('fs-documents-list');
    listEl.innerHTML = '';

    if (!activeCollection) {
        listEl.innerHTML = `<div class="fs-empty-hint">Select a collection on the left</div>`;
        return;
    }

    if (docs.length === 0) {
        listEl.innerHTML = `<div class="fs-empty-hint">No documents in "${activeCollection}".<br><br>Click "+ Add Document" to insert one.</div>`;
        return;
    }

    docs.forEach(doc => {
        const item = document.createElement('div');
        item.className = `doc-item ${doc._id === activeDocumentId ? 'active' : ''}`;
        item.onclick = () => selectDocument(doc._id);
        item.innerHTML = `
            <div class="doc-item-title">${doc._id}</div>
            <button class="btn btn-xs btn-ghost" onclick="event.stopPropagation(); handleDeleteDocument('${doc._id}')" title="Delete Document">
                <svg width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
                    <polyline points="3 6 5 6 21 6"></polyline>
                    <path d="M19 6v14a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V6"></path>
                </svg>
            </button>
        `;
        listEl.appendChild(item);
    });
}

// Select a Document & Load into Inspector (Column 3)
function selectDocument(docId) {
    activeDocumentId = docId;
    const doc = studioDocuments.find(d => d._id === docId);

    // Update highlights in Column 2
    document.querySelectorAll('.doc-item').forEach(el => {
        if (el.querySelector('.doc-item-title').textContent === docId) {
            el.classList.add('active');
        } else {
            el.classList.remove('active');
        }
    });

    renderDocumentInspector(doc);
}

// Render Document Inspector Fields (Column 3)
function renderDocumentInspector(doc) {
    const pill = document.getElementById('fs-active-doc-id-pill');
    const body = document.getElementById('fs-inspector-body');
    const btnAddField = document.getElementById('btn-add-field');
    const btnDeleteDoc = document.getElementById('btn-delete-doc');

    if (!doc) {
        pill.textContent = 'No document selected';
        btnAddField.disabled = true;
        btnDeleteDoc.disabled = true;
        body.innerHTML = `<div class="fs-empty-hint">Select a document from the center column to inspect and edit fields.</div>`;
        return;
    }

    pill.textContent = doc._id;
    btnAddField.disabled = false;
    btnDeleteDoc.disabled = false;

    body.innerHTML = '';

    const entries = Object.entries(doc);
    entries.forEach(([key, val]) => {
        const isId = key === '_id';
        const type = typeof val;
        let typeBadgeClass = 'type-string';
        let displayType = type;

        if (isId) {
            typeBadgeClass = 'type-id';
            displayType = 'id';
        } else if (type === 'number') {
            typeBadgeClass = 'type-number';
        } else if (type === 'boolean') {
            typeBadgeClass = 'type-boolean';
        } else if (type === 'object') {
            typeBadgeClass = 'type-object';
            displayType = Array.isArray(val) ? 'array' : 'object';
        }

        const card = document.createElement('div');
        card.className = 'doc-field-card';
        card.id = `field-card-${key}`;

        const formattedVal = typeof val === 'object' ? JSON.stringify(val) : String(val);

        card.innerHTML = `
            <div class="doc-field-info">
                <span class="field-type-pill ${typeBadgeClass}">${displayType}</span>
                <span class="doc-field-name">${key}</span>
            </div>
            <div class="doc-field-value" id="field-val-text-${key}">${escapeHtml(formattedVal)}</div>
            <div class="doc-field-actions">
                ${!isId ? `
                    <button class="btn btn-xs btn-ghost" onclick="startEditField('${key}', ${JSON.stringify(val)})">Edit</button>
                    <button class="btn btn-xs btn-ghost" onclick="handleDeleteField('${key}')" title="Delete Field">
                        <svg width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
                            <polyline points="3 6 5 6 21 6"></polyline>
                            <path d="M19 6v14a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V6"></path>
                        </svg>
                    </button>
                ` : `
                    <button class="btn btn-xs btn-ghost" onclick="copyText('${doc._id}', 'Document ID')">Copy</button>
                `}
            </div>
        `;
        body.appendChild(card);
    });
}

// Inline Field Editing
function startEditField(fieldName, currentVal) {
    const valTextEl = document.getElementById(`field-val-text-${fieldName}`);
    const cardEl = document.getElementById(`field-card-${fieldName}`);
    const actionsEl = cardEl.querySelector('.doc-field-actions');

    const strVal = typeof currentVal === 'object' ? JSON.stringify(currentVal) : String(currentVal);

    valTextEl.innerHTML = `
        <input type="text" id="edit-input-${fieldName}" value="${escapeHtml(strVal)}" style="width: 100%; background: #060A13; border: 1px solid var(--primary); color: #FFF; font-family: var(--font-mono); font-size: 0.8rem; padding: 0.25rem 0.5rem; border-radius: 4px;">
    `;

    actionsEl.innerHTML = `
        <button class="btn btn-xs btn-primary" onclick="saveEditField('${fieldName}')">Save</button>
        <button class="btn btn-xs btn-secondary" onclick="renderDocumentInspector(studioDocuments.find(d => d._id === activeDocumentId))">Cancel</button>
    `;

    document.getElementById(`edit-input-${fieldName}`).focus();
}

async function saveEditField(fieldName) {
    const input = document.getElementById(`edit-input-${fieldName}`);
    if (!input) return;
    const rawVal = input.value.trim();

    let parsedVal = rawVal;
    // Auto-detect numbers, booleans, and JSON
    if (rawVal === 'true') parsedVal = true;
    else if (rawVal === 'false') parsedVal = false;
    else if (!isNaN(Number(rawVal)) && rawVal !== '') parsedVal = Number(rawVal);
    else if ((rawVal.startsWith('{') && rawVal.endsWith('}')) || (rawVal.startsWith('[') && rawVal.endsWith(']'))) {
        try { parsedVal = JSON.parse(rawVal); } catch {}
    }

    try {
        await emitRequest('document:update', {
            databaseId: activeStudioDb.databaseId,
            author: activeStudioDb.author,
            apiKey: activeStudioDb.apiKey,
            collection: activeCollection,
            id: activeDocumentId,
            data: { [fieldName]: parsedVal }
        });

        // Update local state
        const doc = studioDocuments.find(d => d._id === activeDocumentId);
        if (doc) doc[fieldName] = parsedVal;

        showToast(`Updated field "${fieldName}"`);
        renderDocumentInspector(doc);
    } catch (err) {
        showToast(err.message || 'Failed to update field', true);
    }
}

// Delete Field from Active Document
async function handleDeleteField(fieldName) {
    if (!confirm(`Are you sure you want to delete field "${fieldName}"?`)) return;

    try {
        await emitRequest('document:deleteField', {
            databaseId: activeStudioDb.databaseId,
            author: activeStudioDb.author,
            apiKey: activeStudioDb.apiKey,
            collection: activeCollection,
            id: activeDocumentId,
            fieldName: fieldName
        });

        const doc = studioDocuments.find(d => d._id === activeDocumentId);
        if (doc) delete doc[fieldName];

        showToast(`Deleted field "${fieldName}"`);
        renderDocumentInspector(doc);
    } catch (err) {
        showToast(err.message || 'Failed to delete field', true);
    }
}

// ============================================================
// TABLE / SPREADSHEET GRID VIEW
// ============================================================
function renderTableView() {
    const select = document.getElementById('table-collection-select');
    const badge = document.getElementById('table-row-count-badge');
    const thead = document.getElementById('fs-table-head');
    const tbody = document.getElementById('fs-table-body');

    select.innerHTML = '';
    studioCollections.forEach(c => {
        const opt = document.createElement('option');
        opt.value = c.name;
        opt.textContent = `${c.name} (${c.count})`;
        if (c.name === activeCollection) opt.selected = true;
        select.appendChild(opt);
    });

    badge.textContent = `${studioDocuments.length} document(s)`;
    thead.innerHTML = '';
    tbody.innerHTML = '';

    if (studioDocuments.length === 0) {
        tbody.innerHTML = `<tr><td colspan="10" style="text-align: center; padding: 3rem; color: var(--text-dim);">No documents in this collection. Click "+ Add Document" to create one.</td></tr>`;
        return;
    }

    // Collect all unique field keys across all documents
    const keySet = new Set(['_id']);
    studioDocuments.forEach(doc => {
        Object.keys(doc).forEach(k => keySet.add(k));
    });
    const columns = Array.from(keySet);

    // Build Table Header
    const trHead = document.createElement('tr');
    columns.forEach(col => {
        const th = document.createElement('th');
        th.textContent = col;
        trHead.appendChild(th);
    });
    const thActions = document.createElement('th');
    thActions.textContent = 'Actions';
    trHead.appendChild(thActions);
    thead.appendChild(trHead);

    // Build Table Rows
    studioDocuments.forEach(doc => {
        const tr = document.createElement('tr');
        columns.forEach(col => {
            const td = document.createElement('td');
            const val = doc[col];
            if (col === '_id') {
                td.className = 'table-doc-id-cell';
                td.textContent = val || '';
            } else if (val === undefined || val === null) {
                td.innerHTML = '<span style="color: var(--text-dim);">-</span>';
            } else if (typeof val === 'object') {
                td.textContent = JSON.stringify(val);
            } else if (typeof val === 'boolean') {
                td.innerHTML = `<span class="field-type-pill type-boolean">${val}</span>`;
            } else if (typeof val === 'number') {
                td.innerHTML = `<span style="color: #FBBF24;">${val}</span>`;
            } else {
                td.textContent = String(val);
            }
            tr.appendChild(td);
        });

        // Action Column
        const tdAct = document.createElement('td');
        tdAct.innerHTML = `
            <div style="display: flex; gap: 0.35rem;">
                <button class="btn btn-xs btn-ghost" onclick="inspectDocFromTable('${doc._id}')">Inspect</button>
                <button class="btn btn-xs btn-danger" onclick="handleDeleteDocument('${doc._id}')">Delete</button>
            </div>
        `;
        tr.appendChild(tdAct);
        tbody.appendChild(tr);
    });
}

function onTableCollectionChange(newCol) {
    selectCollection(newCol);
}

function inspectDocFromTable(docId) {
    setStudioViewMode('columns');
    selectDocument(docId);
}

// ============================================================
// MODAL HANDLERS
// ============================================================

// Add Collection
function openAddCollectionModal() {
    document.getElementById('add-collection-modal').classList.remove('hidden');
    document.getElementById('new-collection-name').value = '';
    document.getElementById('new-collection-name').focus();
}

function closeAddCollectionModal() {
    document.getElementById('add-collection-modal').classList.add('hidden');
}

async function handleAddCollectionSubmit(e) {
    e.preventDefault();
    const name = document.getElementById('new-collection-name').value.trim();
    if (!name) return;

    try {
        await emitRequest('collection:create', {
            databaseId: activeStudioDb.databaseId,
            author: activeStudioDb.author,
            apiKey: activeStudioDb.apiKey,
            name: name
        });
        showToast(`Collection "${name}" created!`);
        closeAddCollectionModal();
        await refreshStudioData();
        await selectCollection(name);
    } catch (err) {
        showToast(err.message || 'Failed to create collection', true);
    }
}

// Delete Collection
async function handleDeleteCollection(colName) {
    if (!confirm(`Are you sure you want to delete collection "${colName}" and all its documents?`)) return;

    try {
        await emitRequest('collection:delete', {
            databaseId: activeStudioDb.databaseId,
            author: activeStudioDb.author,
            apiKey: activeStudioDb.apiKey,
            name: colName
        });
        showToast(`Collection "${colName}" deleted.`);
        await refreshStudioData();
    } catch (err) {
        showToast(err.message || 'Failed to delete collection', true);
    }
}

// Add Document Modal
function openAddDocumentModal() {
    if (!activeCollection) {
        showToast('Please create or select a collection first', true);
        return;
    }
    document.getElementById('modal-doc-col-title').textContent = activeCollection;
    document.getElementById('new-doc-id').value = '';
    document.getElementById('add-document-modal').classList.remove('hidden');
}

function closeAddDocumentModal() {
    document.getElementById('add-document-modal').classList.add('hidden');
}

async function handleAddDocumentSubmit(e) {
    e.preventDefault();
    const customId = document.getElementById('new-doc-id').value.trim() || undefined;
    const jsonStr = document.getElementById('new-doc-json').value;

    let payload;
    try {
        payload = JSON.parse(jsonStr);
    } catch (err) {
        showToast('Invalid JSON payload: ' + err.message, true);
        return;
    }

    try {
        const res = await emitRequest('document:set', {
            databaseId: activeStudioDb.databaseId,
            author: activeStudioDb.author,
            apiKey: activeStudioDb.apiKey,
            collection: activeCollection,
            id: customId,
            data: payload
        });

        showToast('Document saved successfully!');
        closeAddDocumentModal();
        await selectCollection(activeCollection);
    } catch (err) {
        showToast(err.message || 'Failed to save document', true);
    }
}

// Delete Document
async function handleDeleteDocument(docId) {
    if (!confirm(`Are you sure you want to delete document "${docId}"?`)) return;

    try {
        await emitRequest('document:delete', {
            databaseId: activeStudioDb.databaseId,
            author: activeStudioDb.author,
            apiKey: activeStudioDb.apiKey,
            collection: activeCollection,
            id: docId
        });
        showToast(`Document deleted.`);
        await selectCollection(activeCollection);
    } catch (err) {
        showToast(err.message || 'Failed to delete document', true);
    }
}

function deleteActiveDocument() {
    if (activeDocumentId) {
        handleDeleteDocument(activeDocumentId);
    }
}

// Add Field Modal
function openAddFieldModal() {
    if (!activeDocumentId) return;
    document.getElementById('new-field-name').value = '';
    document.getElementById('new-field-value').value = '';
    document.getElementById('add-field-modal').classList.remove('hidden');
    document.getElementById('new-field-name').focus();
}

function closeAddFieldModal() {
    document.getElementById('add-field-modal').classList.add('hidden');
}

function onFieldTypeChange(type) {
    const valInput = document.getElementById('new-field-value');
    if (type === 'boolean') {
        valInput.placeholder = 'true or false';
    } else if (type === 'number') {
        valInput.placeholder = 'e.g. 42 or 99.95';
    } else if (type === 'json') {
        valInput.placeholder = '{"key": "value"} or [1, 2, 3]';
    } else {
        valInput.placeholder = 'Enter string value';
    }
}

async function handleAddFieldSubmit(e) {
    e.preventDefault();
    const name = document.getElementById('new-field-name').value.trim();
    const type = document.getElementById('new-field-type').value;
    const rawVal = document.getElementById('new-field-value').value.trim();

    if (!name) return;

    let val = rawVal;
    if (type === 'number') val = Number(rawVal);
    else if (type === 'boolean') val = rawVal.toLowerCase() === 'true';
    else if (type === 'json') {
        try { val = JSON.parse(rawVal); } catch (err) {
            showToast('Invalid JSON: ' + err.message, true);
            return;
        }
    }

    try {
        await emitRequest('document:update', {
            databaseId: activeStudioDb.databaseId,
            author: activeStudioDb.author,
            apiKey: activeStudioDb.apiKey,
            collection: activeCollection,
            id: activeDocumentId,
            data: { [name]: val }
        });

        const doc = studioDocuments.find(d => d._id === activeDocumentId);
        if (doc) doc[name] = val;

        showToast(`Field "${name}" added!`);
        closeAddFieldModal();
        renderDocumentInspector(doc);
    } catch (err) {
        showToast(err.message || 'Failed to add field', true);
    }
}

// ============================================================
// DATABASE CREATION & DELETION
// ============================================================
function openCreateDbModal() {
    document.getElementById('create-db-modal').classList.remove('hidden');
    document.getElementById('new-db-name').value = '';
    document.getElementById('new-db-name').focus();
}

function closeCreateDbModal() {
    document.getElementById('create-db-modal').classList.add('hidden');
}

async function handleCreateDatabase(e) {
    e.preventDefault();
    const btn = document.getElementById('create-db-submit-btn');
    const name = document.getElementById('new-db-name').value.trim();

    if (!name) return;

    btn.disabled = true;
    btn.textContent = 'Creating...';

    try {
        await emitRequest('database:create', {
            userId: currentUser.uid,
            name: name
        });

        showToast(`Database "${name}" created!`);
        closeCreateDbModal();
        loadDatabases();
    } catch (err) {
        showToast(err.message || 'Failed to create database', true);
    } finally {
        btn.disabled = false;
        btn.textContent = 'Create Database';
    }
}

async function handleDeleteDatabase(dbId, dbName) {
    if (!confirm(`Are you sure you want to permanently delete database "${dbName}"? All data will be wiped.`)) {
        return;
    }

    try {
        await emitRequest('database:delete', {
            userId: currentUser.uid,
            databaseId: dbId
        });

        showToast(`Database "${dbName}" deleted.`);
        loadDatabases();
    } catch (err) {
        showToast(err.message || 'Failed to delete database', true);
    }
}

// Utility to escape HTML strings
function escapeHtml(str) {
    if (str === null || str === undefined) return '';
    return String(str)
        .replace(/&/g, '&amp;')
        .replace(/</g, '&lt;')
        .replace(/>/g, '&gt;')
        .replace(/"/g, '&quot;')
        .replace(/'/g, '&#039;');
}

// App Initialization
window.addEventListener('DOMContentLoaded', () => {
    initSocket();

    const savedUser = localStorage.getItem('fastdb_user');
    if (savedUser) {
        try {
            currentUser = JSON.parse(savedUser);
            renderLoggedInState();
        } catch {
            renderLoggedOutState();
        }
    } else {
        renderLoggedOutState();
    }
});
