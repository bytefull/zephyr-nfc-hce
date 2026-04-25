# Zephyr NFC HCE

This is a PoC (Proof of Concept) for establishing communication between an STM32+PN532 NFC module and an Android phone.

## ✅ TODO

- [ ] Find an easier more cleaner way of managing the rx_len

- [ ] Maybe add semaphore instead odf using delays

- [ ] Encapsulate the waiting for response after each command into a function (eg: pn532_read_response)

- [ ] Organize the code in separate pn532.c and pn532.h files with clean APIs

- [ ] Start to work on structuring those files as a zephyr out of tree driver

- [ ] Start work on the application and the challenge response protocol using the APDUs
