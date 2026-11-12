hoist_module(net
    SOURCES src/Hash.cpp src/HttpClient.cpp src/Json.cpp src/NativeNet.cpp
    LIBS MbedTLS::mbedtls MbedTLS::mbedx509 MbedTLS::mbedcrypto)
