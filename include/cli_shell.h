#pragma once

#include "config.h"
#include <Arduino.h>

class CliShell {
public:
    static CliShell& instance();

    void init();
    void update(); // Poll serial in loop

private:
    CliShell();
    String rxBuffer;

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
};
