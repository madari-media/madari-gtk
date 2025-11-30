#include "application.hpp"

int main(int argc, char *argv[]) {
    g_autoptr(MadariApplication) app = madari_application_new();
    return g_application_run(G_APPLICATION(app), argc, argv);
}

