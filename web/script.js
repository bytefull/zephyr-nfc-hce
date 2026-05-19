const ledSwitch = document.getElementById("ledSwitch");
const ledStatus = document.getElementById("ledStatus");
const iconOn    = document.getElementById("iconOn");
const iconOff   = document.getElementById("iconOff");

function updateIcon(on) {
    ledStatus.textContent = on ? "On" : "Off";
    iconOn.classList.toggle("d-none", !on);
    iconOff.classList.toggle("d-none", on);
}

ledSwitch.addEventListener("change", async () => {
    const on = ledSwitch.checked;
    updateIcon(on);

    try {
        const response = await fetch("/led", {
            method: "POST",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify({ led: on })
        });
        console.log("Server response:", response.status);
    } catch (err) {
        console.error("Request failed:", err);
        // Revert UI on failure
        ledSwitch.checked = !on;
        updateIcon(!on);
    }
});

async function syncLedState() {
    try {
        const response = await fetch("/led");
        const data = await response.json();

        ledSwitch.checked = data.led;

        updateIcon(data.led);

        console.log("Synced LED state:", data);
    } catch (err) {
        console.error("Failed to sync LED state:", err);
    }
}

syncLedState();
