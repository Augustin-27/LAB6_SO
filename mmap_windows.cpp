#include <iostream>
#include <windows.h>
#include <string>
#include <cstdlib>
#include <ctime>

#define SHM_NAME "Local\\SHM_Counter_App"
#define SEM_NAME "Local\\SEM_Counter_App"
#define MAX_COUNT 1000

// Funcție executată de procesele copil pe Windows
void run_child_process(int process_id) {
    // 1. Deschide obiectul de memorie partajată creat de părinte
    HANDLE hMapFile = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, SHM_NAME);
    if (!hMapFile) {
        std::cerr << "[Copil " << process_id << "] Eroare la OpenFileMappingA!\n";
        return;
    }

    // Mapare memorie în spațiul procesului curent
    int* shared_count = (int*)MapViewOfFile(hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(int));
    if (!shared_count) {
        std::cerr << "[Copil " << process_id << "] Eroare la MapViewOfFile!\n";
        CloseHandle(hMapFile);
        return;
    }

    // 2. Deschide semaforul creat de părinte
    HANDLE hSem = OpenSemaphoreA(SYNCHRONIZE | SEMAPHORE_MODIFY_STATE, FALSE, SEM_NAME);
    if (!hSem) {
        std::cerr << "[Copil " << process_id << "] Eroare la OpenSemaphoreA!\n";
        UnmapViewOfFile(shared_count);
        CloseHandle(hMapFile);
        return;
    }

    // Generator numere aleatorii specific procesului
    std::srand((unsigned int)time(NULL) ^ (GetCurrentProcessId() << 16));

    while (true) {
        // REZERVARE SEMAFOR (Intrare în regiunea critică)
        WaitForSingleObject(hSem, INFINITE);

        if (*shared_count >= MAX_COUNT) {
            ReleaseSemaphore(hSem, 1, NULL); // Eliberează semaforul pentru celălalt proces
            break;
        }

        // DAU CU BANUL: cât timp cade 2, scriu în memorie următorul număr
        while (*shared_count < MAX_COUNT) {
            int coin = (std::rand() % 2) + 1;

            if (coin == 2) {
                (*shared_count)++;
                std::cout << "[Proces " << process_id << " | PID: " << GetCurrentProcessId() 
                          << "] A dat 2 -> A scris in memorie: " << *shared_count << "\n";
            } else {
                std::cout << "[Proces " << process_id << " | PID: " << GetCurrentProcessId() 
                          << "] A dat 1 -> Cedeaza randul.\n";
                break;
            }
        }

        // ELIBERARE SEMAFOR (Ieșire din regiunea critică)
        ReleaseSemaphore(hSem, 1, NULL);

        Sleep(1); // Pauză scurtă de 1 ms
    }

    // Curățare resurse din procesul copil
    UnmapViewOfFile(shared_count);
    CloseHandle(hMapFile);
    CloseHandle(hSem);
}

int main(int argc, char* argv[]) {
    // Dacă procesul este lansat ca proces copil (primește argumentul "child ID")
    if (argc == 3 && std::string(argv[1]) == "child") {
        int id = std::stoi(argv[2]);
        run_child_process(id);
        return 0;
    }

    // === PROCES PĂRINȚI ===
    std::cout << "--- Start Numaratoare 1 - 1000 pe Windows (CreateFileMapping + CreateSemaphore) ---\n\n";

    // 1. Creează Obiectul Mapat în Memorie (Shared Memory)
    HANDLE hMapFile = CreateFileMappingA(
        INVALID_HANDLE_VALUE,   // Memorie RAM de sistem
        NULL,                   // Securitate implicită
        PAGE_READWRITE,         // Acces citire/scriere
        0, 
        sizeof(int),            // Dimensiune memorie (un int)
        SHM_NAME                // Numele resursei partajate
    );

    if (!hMapFile) {
        std::cerr << "Eroare la crearea CreateFileMappingA!\n";
        return 1;
    }

    // Mapare pointer în memoria procesului părinte
    int* shared_count = (int*)MapViewOfFile(hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(int));
    *shared_count = 0; // Inițializare contor cu 0

    // 2. Creează Semaforul cu valoarea inițială 1 (funcționează ca Mutex)
    HANDLE hSem = CreateSemaphoreA(NULL, 1, 1, SEM_NAME);
    if (!hSem) {
        std::cerr << "Eroare la crearea CreateSemaphoreA!\n";
        UnmapViewOfFile(shared_count);
        CloseHandle(hMapFile);
        return 1;
    }

    // Calea către executabilul curent
    char exePath[MAX_PATH];
    GetModuleFileNameA(NULL, exePath, MAX_PATH);

    // 3. Crearea celor 2 procese copil individuale
    PROCESS_INFORMATION pi[2];
    STARTUPINFOA si[2];
    HANDLE hProcesses[2];

    for (int i = 0; i < 2; ++i) {
        ZeroMemory(&si[i], sizeof(STARTUPINFOA));
        si[i].cb = sizeof(STARTUPINFOA);
        ZeroMemory(&pi[i], sizeof(PROCESS_INFORMATION));

        std::string cmd = "\"" + std::string(exePath) + "\" child " + std::to_string(i + 1);

        CreateProcessA(
            NULL, &cmd[0], NULL, NULL, FALSE, 0, NULL, NULL, &si[i], &pi[i]
        );

        hProcesses[i] = pi[i].hProcess;
        CloseHandle(pi[i].hThread);
    }

    // Părintele așteaptă ca ambele procese copil să termine
    WaitForMultipleObjects(2, hProcesses, TRUE, INFINITE);

    std::cout << "\n============================================\n";
    std::cout << " Numaratoare finalizata! Valoare finala memorie: " << *shared_count << "\n";
    std::cout << "============================================\n";

    // Curățare resurse părinte
    for (int i = 0; i < 2; ++i) {
        CloseHandle(pi[i].hProcess);
    }

    UnmapViewOfFile(shared_count);
    CloseHandle(hMapFile);
    CloseHandle(hSem);

    return 0;
}