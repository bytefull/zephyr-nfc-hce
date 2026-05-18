const buzzerSwitch = document.getElementById("buzzerSwitch");

buzzerSwitch.addEventListener("change", async () => {

    const payload = {
        buzzer: buzzerSwitch.checked
    };

    console.log("Sending:", payload);

    try {
        const response = await fetch("/buzzer", {
            method: "POST",
            headers: {
                "Content-Type": "application/json"
            },
            body: JSON.stringify(payload)
        });

        console.log("Server response:", response.status);

    } catch (err) {
        console.error("Request failed:", err);
    }
});