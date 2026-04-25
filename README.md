# Zephyr NFC HCE

This is a PoC (Proof of Concept) for establishing communication between an STM32+PN532 NFC module and an Android phone.

## ✅ TODO

- [x] Verify APDU responses

- [x] Adding a ring buffer instead of managing a raw buffer and its rx length

- [ ] Encapsulate the waiting for response after each command into a function (eg: pn532_read_response)

- [ ] Organize the code in separate pn532.c and pn532.h files with clean APIs

- [ ] Start to work on structuring those files as a zephyr out of tree driver

- [ ] Start work on the application and the challenge response protocol using the APDUs

- [ ] Increase UART baudrate to 921600 in initialization, right after SAMConfig
