// ========== WiFi-Sense - V1 Standalone ==========

const HISTORY_LEN = 200;
let ws = null;
let connected = false;

// State
let state = {
    rssi: 0,
    csiAvg: 0,
    motionLevel: 0,
    csiMotionLevel: 0,
    presence: false,
    motion: false,
    csiVar: 0,
    rssiVar: 0
};

// Client-side EMA smoothing for chart display
let smoothRssiMotion = 0;
let smoothCsiMotion = 0;
const CHART_EMA_ALPHA = 0.15;

// Room config (meters)
let room = { width: 5.0, length: 4.0 };

// History for chart
let history = {
    rssiMotion: [],
    csiMotion: []
};

// Canvas refs
let roomCanvas, roomCtx;
let histCanvas, histCtx;

// ========== WebSocket ==========

function connectWS() {
    const host = window.location.hostname || '192.168.4.1';
    const url = 'ws://' + host + '/ws';
    ws = new WebSocket(url);

    ws.onopen = function() {
        connected = true;
        updateConnectionBadge(true);
        console.log('WebSocket connected');
    };

    ws.onclose = function() {
        connected = false;
        updateConnectionBadge(false);
        console.log('WebSocket closed, reconnecting...');
        setTimeout(connectWS, 2000);
    };

    ws.onerror = function(e) {
        console.log('WebSocket error', e);
        ws.close();
    };

    ws.onmessage = function(event) {
        try {
            const data = JSON.parse(event.data);
            state = { ...state, ...data };
            updateUI();
        } catch(e) {
            console.log('Parse error', e);
        }
    };
}

function updateConnectionBadge(conn) {
    const badge = document.getElementById('connBadge');
    if (conn) {
        badge.className = 'status-badge connected';
        badge.textContent = 'CONNECTED';
    } else {
        badge.className = 'status-badge disconnected';
        badge.textContent = 'DISCONNECTED';
    }
}

// ========== UI Updates ==========

function updateUI() {
    // Update detection indicator
    const icon = document.getElementById('detIcon');
    const label = document.getElementById('detLabel');
    const sub = document.getElementById('detSub');

    if (state.motion) {
        icon.className = 'detection-icon motion';
        icon.textContent = '\u26A1';
        label.textContent = 'MOTION DETECTED';
        label.style.color = '#d2992a';
        sub.textContent = 'Active movement in room';
    } else if (state.presence) {
        icon.className = 'detection-icon presence';
        icon.textContent = '\u{1F9D1}';
        label.textContent = 'PRESENCE DETECTED';
        label.style.color = '#3fb950';
        sub.textContent = 'Someone is in the room';
    } else {
        icon.className = 'detection-icon none';
        icon.textContent = '\u2014';
        label.textContent = 'ROOM EMPTY';
        label.style.color = '#8b949e';
        sub.textContent = 'No presence detected';
    }

    // Update metrics
    document.getElementById('valRSSI').textContent = state.rssi;
    document.getElementById('valCSI').textContent = state.csiAvg.toFixed(1);
    document.getElementById('valMotion').textContent = state.motionLevel;
    document.getElementById('valCSIMotion').textContent = state.csiMotionLevel;

    // Smooth values for chart display
    smoothRssiMotion = smoothRssiMotion * (1 - CHART_EMA_ALPHA) + state.motionLevel * CHART_EMA_ALPHA;
    smoothCsiMotion = smoothCsiMotion * (1 - CHART_EMA_ALPHA) + state.csiMotionLevel * CHART_EMA_ALPHA;

    // Push smoothed values to history
    history.rssiMotion.push(Math.round(smoothRssiMotion));
    history.csiMotion.push(Math.round(smoothCsiMotion));
    if (history.rssiMotion.length > HISTORY_LEN) history.rssiMotion.shift();
    if (history.csiMotion.length > HISTORY_LEN) history.csiMotion.shift();

    // Redraw canvases
    drawRoom();
    drawHistory();
}

// ========== Room Canvas ==========

function drawRoom() {
    if (!roomCtx) return;
    const c = roomCanvas;
    const w = c.width;
    const h = c.height;

    roomCtx.clearRect(0, 0, w, h);

    // Room background
    const pad = 40;
    const rw = w - pad * 2;
    const rh = h - pad * 2;

    // Floor
    roomCtx.fillStyle = '#1a2332';
    roomCtx.fillRect(pad, pad, rw, rh);

    // Walls
    roomCtx.strokeStyle = '#3a4a5e';
    roomCtx.lineWidth = 3;
    roomCtx.strokeRect(pad, pad, rw, rh);

    // Grid lines
    roomCtx.strokeStyle = '#1e2a3a';
    roomCtx.lineWidth = 0.5;
    const gridStep = rw / 10;
    for (let i = 1; i < 10; i++) {
        roomCtx.beginPath();
        roomCtx.moveTo(pad + i * gridStep, pad);
        roomCtx.lineTo(pad + i * gridStep, pad + rh);
        roomCtx.stroke();
    }
    const gridStepY = rh / 8;
    for (let i = 1; i < 8; i++) {
        roomCtx.beginPath();
        roomCtx.moveTo(pad, pad + i * gridStepY);
        roomCtx.lineTo(pad + rw, pad + i * gridStepY);
        roomCtx.stroke();
    }

    // Dimensions
    roomCtx.fillStyle = '#6e7681';
    roomCtx.font = '11px sans-serif';
    roomCtx.textAlign = 'center';
    roomCtx.fillText(room.width + 'm', pad + rw / 2, h - 8);
    roomCtx.save();
    roomCtx.translate(12, pad + rh / 2);
    roomCtx.rotate(-Math.PI / 2);
    roomCtx.fillText(room.length + 'm', 0, 0);
    roomCtx.restore();

    // Router icon (top-left corner)
    drawDevice(roomCtx, pad + 30, pad + 30, '\u{1F4F6}', 'Router', '#58a6ff');

    // ESP32 icon (bottom-right corner)
    drawDevice(roomCtx, pad + rw - 30, pad + rh - 30, '\u{1F4E1}', 'ESP32-S3', '#d2992a');

    // Line of sight between router and ESP32
    roomCtx.setLineDash([6, 4]);
    roomCtx.strokeStyle = state.presence ? 'rgba(63,185,80,0.4)' : 'rgba(88,166,255,0.2)';
    roomCtx.lineWidth = 1.5;
    roomCtx.beginPath();
    roomCtx.moveTo(pad + 30, pad + 30);
    roomCtx.lineTo(pad + rw - 30, pad + rh - 30);
    roomCtx.stroke();
    roomCtx.setLineDash([]);

    // Detection visualization
    if (state.presence || state.motion) {
        // Interference zone - pulsing ellipse along the line of sight
        const cx = pad + rw / 2;
        const cy = pad + rh / 2;
        const motionIntensity = Math.min(state.csiMotionLevel || state.motionLevel, 200) / 200;

        // Ripple rings
        const now = Date.now() / 1000;
        for (let ring = 0; ring < 3; ring++) {
            const phase = (now * 1.5 + ring * 0.7) % 2;
            const radius = 20 + phase * 60;
            const alpha = Math.max(0, 0.3 - phase * 0.15) * motionIntensity;
            const color = state.motion ? `rgba(210,153,34,${alpha})` : `rgba(63,185,80,${alpha})`;

            roomCtx.beginPath();
            roomCtx.ellipse(cx, cy, radius * 1.3, radius * 0.8, Math.PI / 4, 0, Math.PI * 2);
            roomCtx.strokeStyle = color;
            roomCtx.lineWidth = 2;
            roomCtx.stroke();
        }

        // Center person indicator (green dot)
        const dotColor = state.motion ? '#d2992a' : '#3fb950';
        const glowColor = state.motion ? 'rgba(210,153,34,0.4)' : 'rgba(63,185,80,0.4)';

        // Glow
        roomCtx.beginPath();
        roomCtx.arc(cx, cy, 20, 0, Math.PI * 2);
        roomCtx.fillStyle = glowColor;
        roomCtx.fill();

        // Dot
        roomCtx.beginPath();
        roomCtx.arc(cx, cy, 8, 0, Math.PI * 2);
        roomCtx.fillStyle = dotColor;
        roomCtx.fill();

        // Label
        roomCtx.fillStyle = '#ffffff';
        roomCtx.font = 'bold 11px sans-serif';
        roomCtx.textAlign = 'center';
        roomCtx.fillText('Person', cx, cy - 26);
    }
}

function drawDevice(ctx, x, y, emoji, label, color) {
    // Background circle
    ctx.beginPath();
    ctx.arc(x, y, 18, 0, Math.PI * 2);
    ctx.fillStyle = color + '22';
    ctx.fill();
    ctx.strokeStyle = color;
    ctx.lineWidth = 1.5;
    ctx.stroke();

    // Emoji
    ctx.font = '16px sans-serif';
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';
    ctx.fillText(emoji, x, y);

    // Label
    ctx.fillStyle = color;
    ctx.font = '10px sans-serif';
    ctx.textBaseline = 'top';
    ctx.fillText(label, x, y + 22);
}

// ========== History Chart ==========

function drawHistory() {
    if (!histCtx) return;
    const c = histCanvas;
    const w = c.width;
    const h = c.height;

    histCtx.clearRect(0, 0, w, h);

    // Background
    histCtx.fillStyle = '#0d1117';
    histCtx.fillRect(0, 0, w, h);

    // Draw both series
    drawSeries(histCtx, history.rssiMotion, w, h, 'rgba(88,166,255,0.7)', 'rgba(88,166,255,0.1)');
    drawSeries(histCtx, history.csiMotion, w, h, 'rgba(210,153,34,0.7)', 'rgba(210,153,34,0.1)');

    // Legend
    histCtx.font = '10px sans-serif';
    histCtx.fillStyle = 'rgba(88,166,255,0.8)';
    histCtx.fillText('\u2014 RSSI Motion', 8, 12);
    histCtx.fillStyle = 'rgba(210,153,34,0.8)';
    histCtx.fillText('\u2014 CSI Motion', 100, 12);

    // Threshold line
    const motionTh = parseInt(document.getElementById('motionTh')?.value || '15');
    const maxVal = Math.max(50, ...history.rssiMotion, ...history.csiMotion);
    const thY = h - (motionTh / maxVal) * h;
    histCtx.setLineDash([4, 3]);
    histCtx.strokeStyle = 'rgba(248,81,73,0.5)';
    histCtx.lineWidth = 1;
    histCtx.beginPath();
    histCtx.moveTo(0, thY);
    histCtx.lineTo(w, thY);
    histCtx.stroke();
    histCtx.setLineDash([]);
}

function drawSeries(ctx, data, w, h, stroke, fill) {
    if (data.length < 2) return;

    const maxVal = Math.max(50, ...history.rssiMotion, ...history.csiMotion);
    const step = w / (HISTORY_LEN - 1);

    ctx.beginPath();
    for (let i = 0; i < data.length; i++) {
        const x = i * step;
        const y = h - (Math.max(0, data[i]) / maxVal) * h;
        if (i === 0) ctx.moveTo(x, y);
        else ctx.lineTo(x, y);
    }
    ctx.strokeStyle = stroke;
    ctx.lineWidth = 1.5;
    ctx.stroke();

    // Fill under curve
    ctx.lineTo((data.length - 1) * step, h);
    ctx.lineTo(0, h);
    ctx.closePath();
    ctx.fillStyle = fill;
    ctx.fill();
}

// ========== Settings ==========

function setupSettings() {
    const motionSlider = document.getElementById('motionTh');
    const presenceSlider = document.getElementById('presenceTh');
    const scanSlider = document.getElementById('scanInterval');
    const roomW = document.getElementById('roomW');
    const roomL = document.getElementById('roomL');

    motionSlider.addEventListener('input', function() {
        document.getElementById('motionThVal').textContent = this.value;
    });
    presenceSlider.addEventListener('input', function() {
        document.getElementById('presenceThVal').textContent = this.value;
    });
    scanSlider.addEventListener('input', function() {
        document.getElementById('scanIntVal').textContent = this.value + 'ms';
    });

    // Send settings on change (debounced)
    let settingsTimer = null;
    function sendSettings() {
        clearTimeout(settingsTimer);
        settingsTimer = setTimeout(function() {
            if (ws && ws.readyState === WebSocket.OPEN) {
                ws.send(JSON.stringify({
                    action: 'setSettings',
                    motionThreshold: parseInt(motionSlider.value),
                    presenceThreshold: parseInt(presenceSlider.value),
                    scanInterval: parseInt(scanSlider.value)
                }));
            }
        }, 300);
    }

    motionSlider.addEventListener('change', sendSettings);
    presenceSlider.addEventListener('change', sendSettings);
    scanSlider.addEventListener('change', sendSettings);

    // Room dimensions
    function sendRoom() {
        room.width = parseFloat(roomW.value) || 5.0;
        room.length = parseFloat(roomL.value) || 4.0;
        drawRoom();
        if (ws && ws.readyState === WebSocket.OPEN) {
            ws.send(JSON.stringify({
                action: 'setRoom',
                roomWidth: room.width,
                roomLength: room.length
            }));
        }
    }

    roomW.addEventListener('change', sendRoom);
    roomL.addEventListener('change', sendRoom);
}

// ========== Canvas Resize ==========

function resizeCanvases() {
    const wrap = document.querySelector('.canvas-wrap');
    if (wrap && roomCanvas) {
        roomCanvas.width = wrap.clientWidth;
        roomCanvas.height = wrap.clientHeight;
        drawRoom();
    }
    const histWrap = document.getElementById('historyCanvas');
    if (histWrap) {
        histCanvas.width = histWrap.parentElement.clientWidth - 32;
        histCanvas.height = 100;
        drawHistory();
    }
}

// ========== Init ==========

window.addEventListener('load', function() {
    roomCanvas = document.getElementById('roomCanvas');
    roomCtx = roomCanvas.getContext('2d');
    histCanvas = document.getElementById('historyCanvas');
    histCtx = histCanvas.getContext('2d');

    resizeCanvases();
    window.addEventListener('resize', resizeCanvases);

    setupSettings();
    connectWS();

    // Animation loop for ripple effects
    function animate() {
        if (state.presence || state.motion) {
            drawRoom();
        }
        requestAnimationFrame(animate);
    }
    animate();
});
