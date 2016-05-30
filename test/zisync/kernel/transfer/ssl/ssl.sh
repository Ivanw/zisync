#!/bin/bash
#########################################################################
# Author: Pang Hai
# Created Time: Fri Sep 12 14:13:25 2014
# File Name: ssl.sh
# Description: 
#########################################################################

#create CA private key
echo "*********************Create CA private key*********************************************"
openssl genrsa -des3 -out ca.key 1024

#create CA certificate
echo "*********************Create CA certificate*********************************************"
openssl req -new -x509 -key ca.key -out ca.crt -days 365

#create server private key
echo "*********************Create server private key*****************************************"
openssl genrsa -des3 -out server.key 1024

#create request of server certificate
echo "*********************Create request of server certificate******************************"
openssl req -new -key server.key -out server.csr

#create server certificate
echo "*********************Create server certificate*****************************************"
openssl x509 -req -days 30 -in server.csr -CA ca.crt -CAkey ca.key -CAcreateserial -out server.crt

#create server pkcs12
echo "*********************Create server pkcs12**********************************************"
openssl pkcs12 -export -in server.crt -inkey server.key -out server.p12 -name demo_server

#create client private key
echo "*********************Create client private key*****************************************"
openssl req -new -newkey rsa:1024 -nodes -out client.req -keyout client.key

#create request of client certificate
echo "*********************Create request of client certificate******************************"
openssl x509 -CA ca.crt -CAkey ca.key -CAserial ca.srl -req -in client.req -out client.pem -days 365

#create client pkcs12
echo "*********************Create client pkcs12**********************************************"
openssl pkcs12 -export -clcerts -in client.pem -inkey client.key -out client.p12 -name client_client
