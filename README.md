# Zephyr NFC HCE

This is a PoC (Proof of Concept) for establishing communication between an STM32+PN532 NFC module and an Android phone.

## ✅ TODO

- [x] Verify APDU responses

- [x] Adding a ring buffer instead of managing a raw buffer and its rx length

- [x] Normalize all functions to fail fast ie: check failed condition first and exit early

- [x] Normalize all functions and variables naming

- [x] Normalize comments + doxygen

- [x] Increase UART baudrate to 921600 in initialization, right after SAMConfig (921600 worked only on native_sim)

- [x] Organize the code in separate pn532.c and pn532.h files with clean APIs

- [x] Start to work on structuring those files as a zephyr out of tree driver

- [ ] Remove unnecessary duplicated doxygen comments in the driver

- [ ] Clarify API namings for the driver (public vs private)

- [ ] Simplify driver CI and make it more lightweight

- [ ] Validate driver unit tests and samples

- [ ] Encapsulate the waiting for response after each command into a function (eg: pn532_read_response)

- [ ] Start work on the application and the challenge response protocol using the APDUs

- [ ] Fix high baudrate problem on STM32 (although works fine on native_sim)
