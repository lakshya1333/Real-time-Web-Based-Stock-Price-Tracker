const WS_URL = 'wss://localhost:8080';
let ws = null;
let chart = null;
const maxDataPoints = 50;
let datasets = {};

// Colors for different stocks
const colors = {
    'AAPL': 'rgb(255, 99, 132)',
    'GOOGL': 'rgb(54, 162, 235)',
    'MSFT': 'rgb(75, 192, 192)',
    'AMZN': 'rgb(255, 159, 64)',
    'TSLA': 'rgb(153, 102, 255)'
};

function initChart() {
    const ctx = document.getElementById('stockChart').getContext('2d');
    chart = new Chart(ctx, {
        type: 'line',
        data: {
            labels: [],
            datasets: []
        },
        options: {
            responsive: true,
            maintainAspectRatio: false,
            animation: {
                duration: 0
            },
            interaction: {
                mode: 'index',
                intersect: false,
            },
            scales: {
                x: {
                    display: true,
                    grid: { color: 'rgba(255, 255, 255, 0.1)' },
                    ticks: { color: '#aaa', maxTicksLimit: 10 }
                },
                y: {
                    display: true,
                    grid: { color: 'rgba(255, 255, 255, 0.1)' },
                    ticks: { color: '#aaa' }
                }
            },
            plugins: {
                legend: { labels: { color: '#fff' } }
            }
        }
    });
}

function logMessage(msg) {
    const logList = document.getElementById('logList');
    const li = document.createElement('li');
    const time = new Date().toLocaleTimeString();
    li.innerHTML = `<span class="time">[${time}]</span> ${msg}`;
    logList.insertBefore(li, logList.firstChild);
    
    if (logList.children.length > 50) {
        logList.removeChild(logList.lastChild);
    }
}

function updateStatus(connected) {
    const statusEl = document.getElementById('status');
    if (connected) {
        statusEl.textContent = 'Connected';
        statusEl.className = 'status connected';
    } else {
        statusEl.textContent = 'Disconnected';
        statusEl.className = 'status disconnected';
    }
}

function connect() {
    logMessage('Attempting to connect to ' + WS_URL);
    
    ws = new WebSocket(WS_URL);
    
    ws.onopen = () => {
        logMessage('WebSocket connection established. Sending auth token...');
        ws.send('auth_token=supersecret');
    };
    
    ws.onmessage = (event) => {
        try {
            const msg = JSON.parse(event.data);
            
            if (msg.type === 'auth') {
                if (msg.status === 'success') {
                    logMessage('Authentication successful. Ready for data.');
                    updateStatus(true);
                } else {
                    logMessage('Authentication failed.');
                    ws.close();
                }
            } else if (msg.type === 'update') {
                updateChart(msg.data);
                if (Math.random() < 0.2) { // Log 1 out of 5 updates to prevent spam
                    logMessage('Stream: Received real-time pricing data for ' + msg.data.length + ' symbols');
                }
            }
        } catch (e) {
            console.error('Failed to parse message', e);
        }
    };
    
    ws.onclose = () => {
        logMessage('WebSocket connection closed. Reconnecting in 3s...');
        updateStatus(false);
        setTimeout(connect, 3000);
    };
    
    ws.onerror = (err) => {
        logMessage('WebSocket error occurred. See console for details.');
        console.error('WS Error:', err);
    };
}

function updateChart(data) {
    const timeLabel = new Date().toLocaleTimeString();
    chart.data.labels.push(timeLabel);
    if (chart.data.labels.length > maxDataPoints) {
        chart.data.labels.shift();
    }

    data.forEach(item => {
        const { symbol, price } = item;
        
        if (!datasets[symbol]) {
            const newDataset = {
                label: symbol,
                data: [],
                borderColor: colors[symbol] || '#fff',
                backgroundColor: 'transparent',
                borderWidth: 2,
                tension: 0.4,
                pointRadius: 0
            };
            chart.data.datasets.push(newDataset);
            datasets[symbol] = newDataset;
        }
        
        datasets[symbol].data.push(price);
        if (datasets[symbol].data.length > maxDataPoints) {
            datasets[symbol].data.shift();
        }
    });

    chart.update();
}

document.addEventListener('DOMContentLoaded', () => {
    initChart();
    connect();
});
