```bash
# 1. Generate P-256 private key
openssl ecparam -name prime256v1 -genkey -noout -out private_key.pem

# 2. Export public key
openssl ec -in private_key.pem -pubout -out public_key.pem

# 3. Export public key as raw uncompressed point (65 bytes)
openssl ec -in private_key.pem -pubout -conv_form uncompressed -outform DER \
| tail -c 65

# 4. Create binary file containing your exact 32-byte nonce
printf '\x00\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0A\x0B\x0C\x0D\x0E\x0F\x10\x11\x12\x13\x14\x15\x16\x17\x18\x19\x1A\x1B\x1C\x1D\x1E\x1F' > nonce.bin

# 5. Sign the nonce using SHA256 + ECDSA P-256
openssl dgst -sha256 -sign private_key.pem -out signature.der nonce.bin

# 6. Verify signature
openssl dgst -sha256 -verify public_key.pem -signature signature.der nonce.bin
```