#pragma once

// El «canal» entre el navegador y Atarraya: el nombre con el que se registra el puente en los navegadores, el puente,
// la ventana invisible que recibe los pedidos y el programa que el puente abre si Atarraya no está abierto.
//
// La copia de prueba (se compila con ATARRAYA_PRUEBAS y no se publica) usa otro canal entero: así la extensión y el
// puente de verdad nunca le pasan descargas a una copia de prueba, y la copia de prueba nunca lee ni cambia la
// conexión de verdad con los navegadores.

#ifdef ATARRAYA_PRUEBAS
#define ATARRAYA_HOST L"com.briznasoft.atarraya.prueba"
#define ATARRAYA_PUENTE L"atarraya-puente-prueba.exe"
#define ATARRAYA_CLASE_MENSAJES L"Atarraya.Mensajes.Prueba"
#define ATARRAYA_PROGRAMA L"Atarraya-prueba.exe"
#else
#define ATARRAYA_HOST L"com.briznasoft.atarraya"
#define ATARRAYA_PUENTE L"atarraya-puente.exe"
#define ATARRAYA_CLASE_MENSAJES L"Atarraya.Mensajes"
#define ATARRAYA_PROGRAMA L"Atarraya.exe"
#endif
