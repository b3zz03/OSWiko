#!/bin/sh
# OSWiko - avvia il finto server Telegram e prepara la configurazione di prova.
# Poi: ./tools/run-mock-qemu.sh in un altro terminale.
set -e
D=build/mock && mkdir -p $D && cd $D
if [ ! -f ca.pem ]; then
	openssl req -x509 -newkey rsa:2048 -nodes -keyout ca.key -out ca.pem -days 365 -subj "/CN=OSWiko Mock CA"
	openssl req -newkey rsa:2048 -nodes -keyout srv.key -out srv.csr -subj "/CN=telegram.test"
	printf "subjectAltName=DNS:telegram.test\nbasicConstraints=CA:FALSE\n" > ext.cnf
	openssl x509 -req -in srv.csr -CA ca.pem -CAkey ca.key -CAcreateserial -out srv.pem -days 365 -extfile ext.cnf
fi
printf "token=TEST123\napi_host=telegram.test\napi_ip=10.0.2.2\napi_port=8443\nca=/sys/firmware/qemu_fw_cfg/by_name/opt/oswiko/test-ca.pem/raw\n" > telegram.conf
exec python3 ../../tools/mock-telegram.py
