#!/bin/fresh
#
# Frosted crypto smoke test. Exercises sha256sum / aes128 / ecdsa.
# Fresh has no conditionals, command substitution, quotes, pipes, or
# heredoc. Each step runs as a single command and the user reads the
# output to judge pass/fail.

echo ==SHA256-KNOWN-VECTOR==
echo expected=ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad
sha256sum /bin/abc

echo ==SHA256-PAYLOAD==
sha256sum /bin/payload.txt

echo ==AES128-ROUNDTRIP==
cat /bin/aes.key > /tmp/aes.key
aes128 -e -k /tmp/aes.key < /bin/payload.txt > /tmp/ct
aes128 -d -k /tmp/aes.key < /tmp/ct > /tmp/pt
echo plaintext-after-roundtrip:
cat /tmp/pt

echo ==ECDSA-KEYGEN==
ecdsa -g -k /tmp/ec.key
ls -l /tmp/ec.key

echo ==ECDSA-SIGN==
ecdsa -s -k /tmp/ec.key < /bin/payload.txt > /tmp/sig
ls -l /tmp/sig

echo ==ECDSA-VERIFY-GOOD==
ecdsa -v /tmp/sig -k /tmp/ec.key < /bin/payload.txt

echo ==ECDSA-VERIFY-TAMPERED==
cat /bin/payload.txt > /tmp/tampered
echo tamper >> /tmp/tampered
ecdsa -v /tmp/sig -k /tmp/ec.key < /tmp/tampered

echo ==DONE==
