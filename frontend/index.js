// ============================================================================
// FRAUDSHIELD FRONTEND BUSINESS LOGIC
// ============================================================================

const API_BASE = "";

// Chart.js settings
Chart.defaults.font.family = 'Outfit';
Chart.defaults.color = getComputedStyle(document.body).getPropertyValue('--text-muted').trim() || '#9ca3af';

// Global Chart Instances
let fraudTrendChart = null;
let txnVolumeChart = null;
let riskDistributionChart = null;
let userActivityChart = null; // Top Flags Chart
let userProfileHeatmapChart = null; // Sidebar Heatmap Chart

// Global Senders Cache
let registeredUsers = [];

// ============================================================================
// INITIALIZATION
// ============================================================================
document.addEventListener("DOMContentLoaded", () => {
    lucide.createIcons();

    // Set up tabs switching
    const menuItems = document.querySelectorAll(".menu-item");
    menuItems.forEach(item => {
        item.addEventListener("click", (e) => {
            e.preventDefault();
            
            menuItems.forEach(i => i.classList.remove("active"));
            document.querySelectorAll(".tab-pane").forEach(pane => pane.classList.remove("active"));

            item.classList.add("active");
            const tabId = item.getAttribute("data-tab");
            const targetPane = document.getElementById(`${tabId}-tab`);
            if (targetPane) {
                targetPane.classList.add("active");
            }

            const titles = {
                dashboard: "Dashboard Overview",
                transactions: "Transaction Audit Ledger",
                leaderboard: "Fraud Risk Leaderboard",
                simulator: "Transaction Simulator Room"
            };
            document.getElementById("page-title").textContent = titles[tabId] || "Dashboard";

            if (tabId === "dashboard") {
                fetchDashboardStats();
            } else if (tabId === "transactions") {
                fetchTransactions();
            } else if (tabId === "leaderboard") {
                fetchLeaderboard();
            }
        });
    });

    // Theme switch listener
    const themeSwitch = document.getElementById("theme-switch");
    themeSwitch.addEventListener("change", () => {
        if (themeSwitch.checked) {
            document.documentElement.setAttribute("data-theme", "dark");
            document.querySelector(".theme-toggle-container span").textContent = "Dark Mode";
        } else {
            document.documentElement.setAttribute("data-theme", "light");
            document.querySelector(".theme-toggle-container span").textContent = "Light Mode";
        }
        updateChartTheme();
    });

    // Load initial views
    fetchDashboardStats();
    fetchUsers();

    setupModals();
    setupForms();
    setupFilters();
    setupSimulator();
});

// ============================================================================
// API FETCH OPERATIONS
// ============================================================================

async function fetchDashboardStats() {
    try {
        const response = await fetch(`${API_BASE}/api/dashboard`);
        if (!response.ok) throw new Error("Dashboard stats failed");
        
        const data = await response.json();
        
        // Update stats
        document.getElementById("stat-total-users").textContent = data.totalUsers;
        document.getElementById("stat-total-txs").textContent = data.totalTransactions;
        document.getElementById("stat-fraud-alerts").textContent = data.fraudAlerts;
        document.getElementById("stat-suspicious-txs").textContent = data.suspiciousTransactions;
        document.getElementById("stat-avg-amount").textContent = formatCurrency(data.avgTransactionAmount);
        document.getElementById("stat-highest-txn").textContent = formatCurrency(data.highestTransaction);

        // Draw Dashboard Charts
        renderDashboardCharts(data.charts);

    } catch (error) {
        console.error("Dashboard error:", error);
    }
}

async function fetchUsers() {
    try {
        const response = await fetch(`${API_BASE}/api/users`);
        if (response.ok) {
            registeredUsers = await response.json();
            populateSendersDropdown();
        }
    } catch (error) {
        console.error("Error fetching users:", error);
    }
}

async function fetchTransactions() {
    const tableBody = document.getElementById("transactions-table-body");
    tableBody.innerHTML = `<tr><td colspan="9" class="text-center py-4"><div class="spinner"></div> Fetching ledger details...</td></tr>`;

    try {
        const search = document.getElementById("filter-search").value;
        const date = document.getElementById("filter-date").value;
        const risk = document.getElementById("filter-risk").value;
        const fraudOnly = document.getElementById("filter-fraud-only").checked;

        const params = new URLSearchParams();
        if (search) params.append("user", search);
        if (date) params.append("date", date);
        if (risk) params.append("risk", risk);
        if (fraudOnly) params.append("fraudOnly", "true");

        const response = await fetch(`${API_BASE}/api/transactions?${params.toString()}`);
        if (!response.ok) throw new Error("Ledger fetch failed");

        const txs = await response.json();
        document.getElementById("filtered-tx-count").textContent = `Showing ${txs.length} transactions`;

        if (txs.length === 0) {
            tableBody.innerHTML = `<tr><td colspan="9" class="text-center py-4 text-muted">No transactions found matching filter parameters.</td></tr>`;
            return;
        }

        tableBody.innerHTML = "";
        txs.forEach(tx => {
            const tr = document.createElement("tr");
            
            let badgeClass = "badge-success";
            let pillClass = "low";
            if (tx.riskLevel === "FRAUD ALERT") {
                badgeClass = "badge-danger";
                pillClass = "high";
            } else if (tx.riskLevel === "SUSPICIOUS") {
                badgeClass = "badge-warning";
                pillClass = "medium";
            }

            tr.innerHTML = `
                <td><strong>${tx.transactionId}</strong></td>
                <td><span class="upi-text clickable-upi font-monospace" data-upi="${tx.senderUpi}">${tx.senderUpi}</span></td>
                <td><span class="upi-text font-monospace">${tx.receiverUpi}</span></td>
                <td><strong>${formatCurrency(tx.amount)}</strong></td>
                <td>${tx.timestamp}</td>
                <td><span class="text-muted">${tx.device}</span></td>
                <td><span class="text-muted">${tx.location}</span></td>
                <td><span class="score-pill ${pillClass}">${tx.riskScore}</span></td>
                <td>
                    <button class="btn btn-secondary btn-sm audit-btn" data-tx='${JSON.stringify(tx).replace(/'/g, "&apos;")}'>
                        Audit
                    </button>
                </td>
            `;
            tableBody.appendChild(tr);
        });

        // Register Click triggers for Details Modal
        document.querySelectorAll(".audit-btn").forEach(btn => {
            btn.addEventListener("click", () => {
                const txData = JSON.parse(btn.getAttribute("data-tx"));
                openRiskDetailModal(txData);
            });
        });

        // Register Click triggers for User Behavior Profile loading
        document.querySelectorAll(".clickable-upi").forEach(cell => {
            cell.addEventListener("click", () => {
                const upi = cell.getAttribute("data-upi");
                fetchUserProfile(upi);
            });
        });

    } catch (error) {
        console.error("Ledger error:", error);
        tableBody.innerHTML = `<tr><td colspan="9" class="text-center py-4 text-danger">Failed to connect to risk engine.</td></tr>`;
    }
}

// Fetch user behavioral profile stats & render sidebar
async function fetchUserProfile(userId) {
    try {
        const response = await fetch(`${API_BASE}/api/user-profile?userId=${encodeURIComponent(userId)}`);
        if (!response.ok) throw new Error("Failed to fetch user profile");

        const data = await response.json();

        // Update fields
        document.getElementById("prof-name").textContent = data.name;
        document.getElementById("prof-upi").textContent = data.userId;
        document.getElementById("prof-avg").textContent = formatCurrency(data.avgAmount);
        document.getElementById("prof-max").textContent = formatCurrency(data.maxAmount);
        document.getElementById("prof-min").textContent = formatCurrency(data.minAmount);

        // Render Trusted contacts chips
        const contactsContainer = document.getElementById("prof-contacts");
        contactsContainer.innerHTML = "";
        
        // Filter contacts by transaction count
        const contactsList = data.trustedContacts || [];
        if (contactsList.length === 0) {
            contactsContainer.innerHTML = `<span class="text-muted text-center py-1">No contacts indexed yet.</span>`;
        } else {
            // Sort by count descending
            contactsList.sort((a,b) => b.count - a.count);
            contactsList.slice(0, 5).forEach(c => {
                const isTrusted = c.count >= 3;
                const chip = document.createElement("span");
                chip.className = `contact-chip ${isTrusted ? 'trusted' : ''}`;
                chip.innerHTML = `<span class="upi-text font-monospace">${c.receiver}</span> <span class="count">${c.count}</span>`;
                contactsContainer.appendChild(chip);
            });
        }

        // Render Hourly Activity bar chart
        renderProfileHeatmap(data.hourlyHeatmap);

        // Display panel
        document.getElementById("profile-viewer-empty").style.display = "none";
        document.getElementById("profile-viewer-content").style.display = "block";

    } catch (error) {
        console.error("Profile view error:", error);
    }
}

async function fetchLeaderboard() {
    const tableBody = document.getElementById("leaderboard-table-body");
    tableBody.innerHTML = `<tr><td colspan="6" class="text-center py-4"><div class="spinner"></div> Extracting heap statistics...</td></tr>`;

    try {
        const response = await fetch(`${API_BASE}/api/leaderboard`);
        if (!response.ok) throw new Error("Leaderboard fetch failed");

        const rank = await response.json();

        if (rank.length === 0) {
            tableBody.innerHTML = `<tr><td colspan="6" class="text-center py-4 text-muted">No suspicious users recorded. All accounts are clean!</td></tr>`;
            return;
        }

        tableBody.innerHTML = "";
        rank.forEach((su, index) => {
            const tr = document.createElement("tr");

            let levelClass = "badge-success";
            let profileLabel = "Safe Profile";
            if (su.maxRiskScore >= 75) {
                levelClass = "badge-danger";
                profileLabel = "High Risk Alert";
            } else if (su.maxRiskScore >= 50) {
                levelClass = "badge-warning";
                profileLabel = "Suspicious Profile";
            }

            tr.innerHTML = `
                <td><span class="rank-number">#${index + 1}</span></td>
                <td><strong>${su.name}</strong></td>
                <td><span class="upi-text font-monospace">${su.userId}</span></td>
                <td><span class="score-pill ${su.maxRiskScore >= 75 ? 'high' : 'medium'}">${su.maxRiskScore}</span></td>
                <td><span class="badge badge-info">${su.fraudCount} critical alerts</span></td>
                <td><span class="badge ${levelClass}">${profileLabel}</span></td>
            `;
            tableBody.appendChild(tr);
        });

    } catch (error) {
        console.error("Leaderboard error:", error);
        tableBody.innerHTML = `<tr><td colspan="6" class="text-center py-4 text-danger">Failed to retrieve priority-heap data.</td></tr>`;
    }
}

// ============================================================================
// CHARTS & VISUALIZATIONS
// ============================================================================
function renderDashboardCharts(chartsData) {
    const isDark = document.documentElement.getAttribute("data-theme") !== "light";
    const textMuted = isDark ? "#9ca3af" : "#64748b";
    const gridColor = isDark ? "rgba(255, 255, 255, 0.05)" : "rgba(0, 0, 0, 0.05)";

    // 1. Fraud Trend Chart
    if (fraudTrendChart) fraudTrendChart.destroy();
    const ctx1 = document.getElementById("fraudTrendChart").getContext("2d");
    const redGlow = ctx1.createLinearGradient(0, 0, 0, 300);
    redGlow.addColorStop(0, 'rgba(239, 68, 68, 0.25)');
    redGlow.addColorStop(1, 'rgba(239, 68, 68, 0.0)');

    fraudTrendChart = new Chart(ctx1, {
        type: 'line',
        data: {
            labels: chartsData.fraudTrend.dates.length > 0 ? chartsData.fraudTrend.dates : ["Standing by"],
            datasets: [{
                label: 'Fraud Alerts',
                data: chartsData.fraudTrend.counts.length > 0 ? chartsData.fraudTrend.counts : [0],
                borderColor: '#ef4444',
                borderWidth: 3,
                fill: true,
                backgroundColor: redGlow,
                tension: 0.4,
                pointBackgroundColor: '#ef4444'
            }]
        },
        options: {
            responsive: true,
            maintainAspectRatio: false,
            plugins: { legend: { display: false } },
            scales: {
                x: { grid: { display: false }, ticks: { color: textMuted } },
                y: { grid: { color: gridColor }, ticks: { color: textMuted, stepSize: 1 } }
            }
        }
    });

    // 2. UPI Transaction Volume
    if (txnVolumeChart) txnVolumeChart.destroy();
    const ctx2 = document.getElementById("txnVolumeChart").getContext("2d");
    const blueGlow = ctx2.createLinearGradient(0, 0, 0, 300);
    blueGlow.addColorStop(0, 'rgba(59, 130, 246, 0.4)');
    blueGlow.addColorStop(1, 'rgba(59, 130, 246, 0.05)');

    txnVolumeChart = new Chart(ctx2, {
        type: 'bar',
        data: {
            labels: chartsData.txnVolume.dates.length > 0 ? chartsData.txnVolume.dates : ["Standing by"],
            datasets: [{
                label: 'Volume (₹)',
                data: chartsData.txnVolume.amounts.length > 0 ? chartsData.txnVolume.amounts : [0],
                backgroundColor: blueGlow,
                borderColor: '#3b82f6',
                borderWidth: 1.5,
                borderRadius: 4
            }]
        },
        options: {
            responsive: true,
            maintainAspectRatio: false,
            plugins: { legend: { display: false } },
            scales: {
                x: { grid: { display: false }, ticks: { color: textMuted } },
                y: { grid: { color: gridColor }, ticks: { color: textMuted } }
            }
        }
    });

    // 3. Risk Profile Distribution
    if (riskDistributionChart) riskDistributionChart.destroy();
    const ctx3 = document.getElementById("riskDistributionChart").getContext("2d");
    riskDistributionChart = new Chart(ctx3, {
        type: 'doughnut',
        data: {
            labels: ['Safe', 'Suspicious', 'Fraud Alerts'],
            datasets: [{
                data: [chartsData.riskDistribution.safe, chartsData.riskDistribution.suspicious, chartsData.riskDistribution.fraud],
                backgroundColor: ['#10b981', '#f59e0b', '#ef4444'],
                borderWidth: isDark ? 2 : 1,
                borderColor: isDark ? '#141621' : '#ffffff'
            }]
        },
        options: {
            responsive: true,
            maintainAspectRatio: false,
            plugins: {
                legend: {
                    position: 'right',
                    labels: { color: textMuted, boxWidth: 12 }
                }
            },
            cutout: '70%'
        }
    });

    // 4. Top Flags & Vulnerabilities (Replacing redundant activity charts)
    if (userActivityChart) userActivityChart.destroy();
    const ctx4 = document.getElementById("userActivityChart").getContext("2d");
    
    const flagLabels = ['Amount Limit', 'Velocity Spikes', 'Mule Dispersion', 'New Receiver', 'Device Anomalies', 'Location Jumps', 'Hour Anomalies'];
    const flagData = [
        chartsData.topReasons.amount || 0,
        chartsData.topReasons.velocity || 0,
        chartsData.topReasons.mule || 0,
        chartsData.topReasons.newReceiver || 0,
        chartsData.topReasons.deviceChange || 0,
        chartsData.topReasons.locationChange || 0,
        chartsData.topReasons.timeAnomaly || 0
    ];

    userActivityChart = new Chart(ctx4, {
        type: 'bar',
        data: {
            labels: flagLabels,
            datasets: [{
                label: 'Rules Met',
                data: flagData,
                backgroundColor: 'rgba(14, 165, 233, 0.45)',
                borderColor: '#0ea5e9',
                borderWidth: 1.5,
                borderRadius: 4
            }]
        },
        options: {
            indexAxis: 'y',
            responsive: true,
            maintainAspectRatio: false,
            plugins: { legend: { display: false } },
            scales: {
                x: { grid: { color: gridColor }, ticks: { color: textMuted, stepSize: 1 } },
                y: { grid: { display: false }, ticks: { color: textMuted } }
            }
        }
    });
}

// Render hourly bar chart (heatmap) in the sidebar
function renderProfileHeatmap(hourlyData) {
    if (userProfileHeatmapChart) userProfileHeatmapChart.destroy();
    const ctx = document.getElementById("userProfileHeatmap").getContext("2d");

    const labels = Array.from({length: 24}, (_, i) => String(i).padStart(2, '0'));
    const isDark = document.documentElement.getAttribute("data-theme") !== "light";
    const textMuted = isDark ? "#9ca3af" : "#64748b";

    userProfileHeatmapChart = new Chart(ctx, {
        type: 'bar',
        data: {
            labels: labels,
            datasets: [{
                data: hourlyData,
                backgroundColor: 'rgba(37, 99, 235, 0.5)',
                borderColor: 'var(--primary)',
                borderWidth: 1,
                borderRadius: 2
            }]
        },
        options: {
            responsive: true,
            maintainAspectRatio: false,
            plugins: { legend: { display: false } },
            scales: {
                x: { ticks: { color: textMuted, font: { size: 9 }, maxRotation: 0 } },
                y: { grid: { display: false }, ticks: { display: false } }
            }
        }
    });
}

function updateChartTheme() {
    const isDark = document.documentElement.getAttribute("data-theme") !== "light";
    const textMuted = isDark ? "#9ca3af" : "#64748b";
    const gridColor = isDark ? "rgba(255, 255, 255, 0.05)" : "rgba(0, 0, 0, 0.05)";

    const chartInstances = [fraudTrendChart, txnVolumeChart, riskDistributionChart, userActivityChart, userProfileHeatmapChart];
    
    chartInstances.forEach(chart => {
        if (chart) {
            if (chart.options.plugins && chart.options.plugins.legend && chart.options.plugins.legend.labels) {
                chart.options.plugins.legend.labels.color = textMuted;
            }
            if (chart.options.scales) {
                if (chart.options.scales.x) {
                    chart.options.scales.x.ticks.color = textMuted;
                    if (chart.options.scales.x.grid) chart.options.scales.x.grid.color = gridColor;
                }
                if (chart.options.scales.y) {
                    chart.options.scales.y.ticks.color = textMuted;
                    if (chart.options.scales.y.grid) chart.options.scales.y.grid.color = gridColor;
                }
            }
            if (chart.config.type === 'doughnut') {
                chart.data.datasets[0].borderColor = isDark ? '#141621' : '#ffffff';
                chart.data.datasets[0].borderWidth = isDark ? 2 : 1;
            }
            chart.update();
        }
    });
}

// ============================================================================
// MODALS
// ============================================================================
function setupModals() {
    const modals = [
        { btn: "add-user-btn", modal: "add-user-modal" },
        { btn: "quick-tx-btn", modal: "new-tx-modal" }
    ];

    modals.forEach(pair => {
        const btn = document.getElementById(pair.btn);
        const modal = document.getElementById(pair.modal);
        if (btn && modal) {
            btn.addEventListener("click", () => {
                modal.classList.add("active");
                if (pair.modal === "new-tx-modal") {
                    fetchUsers();
                }
            });
        }
    });

    document.querySelectorAll(".close-modal, .close-modal-btn").forEach(btn => {
        btn.addEventListener("click", () => {
            document.querySelectorAll(".modal").forEach(m => m.classList.remove("active"));
        });
    });

    window.addEventListener("click", (e) => {
        if (e.target.classList.contains("modal")) {
            e.target.classList.remove("active");
        }
    });
}

function populateSendersDropdown() {
    const select = document.getElementById("tx-sender");
    if (!select) return;
    
    select.innerHTML = '<option value="">Select registered customer...</option>';
    registeredUsers.forEach(u => {
        const opt = document.createElement("option");
        opt.value = u.userId;
        opt.textContent = `${u.name} (${u.userId})`;
        select.appendChild(opt);
    });

    select.addEventListener("change", (e) => {
        const userId = e.target.value;
        const user = registeredUsers.find(u => u.userId === userId);
        if (user) {
            document.getElementById("tx-device").value = user.lastDevice || "";
            document.getElementById("tx-location").value = user.lastLocation || "";
        }
    });
}

function setupForms() {
    const addUserForm = document.getElementById("add-user-form");
    if (addUserForm) {
        addUserForm.addEventListener("submit", async (e) => {
            e.preventDefault();
            const payload = {
                userId: document.getElementById("user-upi").value,
                name: document.getElementById("user-name").value,
                phone: document.getElementById("user-phone").value,
                lastDevice: document.getElementById("user-device").value,
                lastLocation: document.getElementById("user-location").value
            };

            try {
                const response = await fetch(`${API_BASE}/api/users`, {
                    method: "POST",
                    headers: { "Content-Type": "application/json" },
                    body: JSON.stringify(payload)
                });
                const resData = await response.json();
                
                if (response.ok) {
                    alert("Customer registered successfully!");
                    addUserForm.reset();
                    document.getElementById("add-user-modal").classList.remove("active");
                    fetchUsers();
                    fetchDashboardStats();
                } else {
                    alert(`Error: ${resData.error}`);
                }
            } catch (error) {
                console.error("Create User error:", error);
                alert("Failed to reach Risk Engine server.");
            }
        });
    }

    const newTxForm = document.getElementById("new-tx-form");
    if (newTxForm) {
        newTxForm.addEventListener("submit", async (e) => {
            e.preventDefault();
            
            let customTime = "";
            const formTimeVal = document.getElementById("tx-time").value;
            if (formTimeVal) {
                const dateObj = new Date(formTimeVal);
                const year = dateObj.getFullYear();
                const month = String(dateObj.getMonth() + 1).padStart(2, '0');
                const date = String(dateObj.getDate()).padStart(2, '0');
                const hour = String(dateObj.getHours()).padStart(2, '0');
                const min = String(dateObj.getMinutes()).padStart(2, '0');
                const sec = String(dateObj.getSeconds()).padStart(2, '0');
                customTime = `${year}-${month}-${date} ${hour}:${min}:${sec}`;
            }

            const payload = {
                senderUpi: document.getElementById("tx-sender").value,
                receiverUpi: document.getElementById("tx-receiver").value,
                amount: parseFloat(document.getElementById("tx-amount").value),
                device: document.getElementById("tx-device").value,
                location: document.getElementById("tx-location").value,
                timestamp: customTime
            };

            try {
                const response = await fetch(`${API_BASE}/api/transactions`, {
                    method: "POST",
                    headers: { "Content-Type": "application/json" },
                    body: JSON.stringify(payload)
                });
                
                const resData = await response.json();
                
                if (response.ok) {
                    newTxForm.reset();
                    document.getElementById("new-tx-modal").classList.remove("active");
                    
                    openRiskDetailModal(resData);
                    
                    fetchDashboardStats();
                    fetchTransactions();
                    fetchLeaderboard();
                } else {
                    alert(`Error: ${resData.error}`);
                }
            } catch (error) {
                console.error("Process Transaction error:", error);
                alert("Failed to submit transaction to Risk Engine.");
            }
        });
    }
}

// Formatted Risk breakdown details (+ adds in red, - reduces in green)
function openRiskDetailModal(tx) {
    const modal = document.getElementById("risk-detail-modal");
    if (!modal) return;

    document.getElementById("risk-modal-txid").textContent = tx.transactionId;
    document.getElementById("radial-score-val").textContent = tx.riskScore;

    const levelHeader = document.getElementById("risk-modal-level");
    levelHeader.textContent = tx.riskLevel;

    const circle = document.querySelector(".risk-score-circle");
    circle.style.borderColor = tx.riskScore >= 75 ? "var(--fraud)" : tx.riskScore >= 50 ? "var(--suspicious)" : "var(--safe)";
    circle.style.boxShadow = tx.riskScore >= 75 ? "0 0 20px rgba(239, 68, 68, 0.4)" : tx.riskScore >= 50 ? "0 0 20px rgba(245, 158, 11, 0.4)" : "0 0 20px rgba(16, 185, 129, 0.3)";
    levelHeader.style.color = tx.riskScore >= 75 ? "var(--fraud)" : tx.riskScore >= 50 ? "var(--suspicious)" : "var(--safe)";

    const reasonsList = document.getElementById("risk-reasons-list");
    reasonsList.innerHTML = "";
    
    tx.riskReasons.forEach(r => {
        const li = document.createElement("li");
        li.textContent = r;
        
        // Apply color classes depending on whether rule adds (+) or subtracts (-) risk
        if (r.startsWith("+")) {
            li.className = "reason-add";
        } else if (r.startsWith("-")) {
            li.className = "reason-sub";
        }
        
        reasonsList.appendChild(li);
    });

    modal.classList.add("active");
}

// ============================================================================
// FILTER CONTROLS
// ============================================================================
function setupFilters() {
    const applyBtn = document.getElementById("apply-filters");
    const clearBtn = document.getElementById("clear-filters");

    if (applyBtn) {
        applyBtn.addEventListener("click", () => {
            fetchTransactions();
        });
    }

    if (clearBtn) {
        clearBtn.addEventListener("click", () => {
            document.getElementById("filter-search").value = "";
            document.getElementById("filter-date").value = "";
            document.getElementById("filter-risk").value = "";
            document.getElementById("filter-fraud-only").checked = false;
            fetchTransactions();
        });
    }
}

// ============================================================================
// SIMULATOR CONTROLLER
// ============================================================================
function setupSimulator() {
    const slider = document.getElementById("sim-count-slider");
    const display = document.getElementById("sim-count-display");
    const simBtn = document.getElementById("run-simulation-btn");
    const clearBtn = document.getElementById("clear-log-btn");
    const consoleLogger = document.getElementById("logger-console");

    if (slider && display) {
        slider.addEventListener("input", (e) => {
            display.textContent = e.target.value;
        });
    }

    if (clearBtn && consoleLogger) {
        clearBtn.addEventListener("click", () => {
            consoleLogger.innerHTML = `<div class="console-line system">[SYSTEM] Console log cleared. Standing by.</div>`;
        });
    }

    if (simBtn) {
        simBtn.addEventListener("click", async () => {
            const count = parseInt(slider.value);
            simBtn.disabled = true;
            simBtn.innerHTML = `<div class="spinner"></div> Process streaming...`;
            
            writeConsoleLog("[SYSTEM] Contacting C++ Risk Engine simulator API...", "system");
            
            try {
                // Fetch current list
                const initialTxResp = await fetch(`${API_BASE}/api/transactions`);
                const initialTxs = initialTxResp.ok ? await initialTxResp.json() : [];
                const initialTxIds = new Set(initialTxs.map(t => t.transactionId));

                // POST generate simulator telemetry
                const response = await fetch(`${API_BASE}/api/simulator`, {
                    method: "POST",
                    headers: { "Content-Type": "application/json" },
                    body: JSON.stringify({ count })
                });

                if (!response.ok) throw new Error("Simulation failed");

                const result = await response.json();
                writeConsoleLog(`[SYSTEM] ${result.message}`, "system");
                writeConsoleLog("[SYSTEM] Downloading simulator telemetry...", "system");

                // Download updated transactions list
                const finalTxResp = await fetch(`${API_BASE}/api/transactions`);
                const finalTxs = finalTxResp.ok ? await finalTxResp.json() : [];

                // Filter out the newly simulated elements
                const newTxs = finalTxs.filter(t => !initialTxIds.has(t.transactionId)).reverse();

                let index = 0;
                
                function streamNext() {
                    if (index < newTxs.length) {
                        const tx = newTxs[index];
                        let logType = "risk-ok";
                        let prefix = "[OK]";
                        
                        if (tx.riskLevel === "FRAUD ALERT") {
                            logType = "risk-alert";
                            prefix = "[CRITICAL FRAUD]";
                        } else if (tx.riskLevel === "SUSPICIOUS") {
                            logType = "risk-warn";
                            prefix = "[WARNING SUSPICIOUS]";
                        }

                        const logMsg = `${prefix} TxID: ${tx.transactionId} | Sender: ${tx.senderUpi} -> Receiver: ${tx.receiverUpi} | Amt: ₹${tx.amount.toFixed(2)} | Risk Score: ${tx.riskScore} | Flag Reason: ${tx.riskReasons.join('; ')}`;
                        writeConsoleLog(logMsg, logType);
                        
                        index++;
                        setTimeout(streamNext, 50); // 50ms intervals
                    } else {
                        writeConsoleLog(`[SYSTEM] Telemetry streaming complete. Processed ${newTxs.length} simulated transactions.`, "system");
                        simBtn.disabled = false;
                        simBtn.innerHTML = `<i class="lucide-play-circle"></i><span>Initiate Simulation Stream</span>`;
                        lucide.createIcons();
                        fetchDashboardStats();
                    }
                }

                streamNext();

            } catch (error) {
                console.error("Simulator error:", error);
                writeConsoleLog(`[ERROR] Simulation process aborted: ${error.message}`, "risk-alert");
                simBtn.disabled = false;
                simBtn.innerHTML = `<span>Initiate Simulation Stream</span>`;
            }
        });
    }
}

function writeConsoleLog(msg, type = "default") {
    const logger = document.getElementById("logger-console");
    if (!logger) return;

    const div = document.createElement("div");
    div.className = `console-line ${type}`;
    div.textContent = msg;
    logger.appendChild(div);
    
    logger.scrollTop = logger.scrollHeight;
}

// ============================================================================
// FORMATTER UTILITIES
// ============================================================================
function formatCurrency(val) {
    return new Intl.NumberFormat('en-IN', {
        style: 'currency',
        currency: 'INR',
        maximumFractionDigits: 2
    }).format(val);
}
