#pragma once

#include "config.h"
#include <Arduino.h>
#include <driver/uart.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

class CliShell {
public:
    static CliShell& instance();

    void init();
    void update();
    bool isReady() const { return ready; }

    void print(const char* str);
    void print(const String& str);
    void println(const char* str = "");
    void println(const String& str);
    void printf(const char* format, ...) __attribute__((format(printf, 2, 3)));

private:
    CliShell();
    String rxBuffer;
    QueueHandle_t uartQueue;
    bool ready;
    uint8_t escState; // 0=normal, 1=got ESC, 2=got ESC [

    void processRxByte(char c);
    void handleCommand(const String& line);
    void printHelp();
    void printStatus();
    void printScan();
    void printClients();
    void handleConnect(const String& args);
    void handleSetTx(const String& args);
    void handleAddKnown(const String& args);
    void handleDelKnown(const String& args);
    void handlePing(const String& args);
    void printSpectrum();
    void printDnsCache();
    void handleKick(const String& args);
    void handleRename(const String& args);
    void handleCrashLog(const String& args);
    void handleWan(const String& args);
    void handleProbe(const String& args);
};
