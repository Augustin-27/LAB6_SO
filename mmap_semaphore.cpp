#include <iostream>
#include <cstdlib>
#include <ctime>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <semaphore.h>

#define SHM_NAME "/shm_counter_app"
#define SEM_NAME "/sem_counter_app"
#define MAX_COUNT 1000

// Funcție executată de fiecare dintre cele 2 procese
void run_process(int process_id, int* shared_count, sem_t* sem) {
    // Inițializare generator numere aleatorii specific fiecărui proces
    std::srand(std::time(nullptr) ^ (getpid() << 16));

    while (true) {
        // 1. REZERVARE SEMAFOR (Intrare în regiunea critică)
        // Niciun alt proces nu mai poate atinge memoria în acest timp
        sem_wait(sem);

        // Citire memorie partajată
        int current_val = *shared_count;

        // Verificare condiție de oprire
        if (current_val >= MAX_COUNT) {
            sem_post(sem); // Eliberează semaforul pentru ca și celălalt proces să se poată opri
            break;
        }

        // 2. DAU CU BANUL: cât timp cade 2, scriu în memorie următorul număr
        // random(2) -> generează 1 sau 2
        while (*shared_count < MAX_COUNT) {
            int coin = (std::rand() % 2) + 1; // 1 = Cap, 2 = Pajură

            if (coin == 2) {
                (*shared_count)++;
                std::cout << "[Proces " << process_id << " | PID: " << getpid() 
                          << "] A dat 2 -> A scris in memorie: " << *shared_count << "\n";
            } else {
                std::cout << "[Proces " << process_id << " | PID: " << getpid() 
                          << "] A dat 1 -> Cedeaza randul.\n";
                break; // Ieși din bucla de scriere când cade 1
            }
        }

        // 3. ELIBERARE SEMAFOR (Ieșire din regiunea critică)
        // Memoria revine într-o stare stabilă și poate fi accesată de alt proces
        sem_post(sem);

        // O mică pauză (1 ms) pentru a preveni monopolizarea semaforului de către un singur proces
        usleep(1000);
    }

    std::cout << "==> Procesul " << process_id << " a terminat executia.\n";
}

int main() {
    // Șterge eventuale resurse rămase din rulări anterioare
    shm_unlink(SHM_NAME);
    sem_unlink(SEM_NAME);

    // 1. Creare Obiect Mapat în Memorie (Shared Memory Object)
    int shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1) {
        std::perror("Eroare la shm_open");
        return 1;
    }

    // Setare dimensiune memorie (pentru un întreg)
    ftruncate(shm_fd, sizeof(int));

    // Mapare în spațiul de adresă al procesului (mmap)
    int* shared_count = (int*)mmap(NULL, sizeof(int), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shared_count == MAP_FAILED) {
        std::perror("Eroare la mmap");
        return 1;
    }

    // Inițializare valoare în memoria partajată
    *shared_count = 0;

    // 2. Creare Semafor POSIX (valoare inițială 1 = Mutex)
    sem_t* sem = sem_open(SEM_NAME, O_CREAT, 0666, 1);
    if (sem == SEM_FAILED) {
        std::perror("Eroare la sem_open");
        return 1;
    }

    std::cout << "--- Start Numaratoare 1 - 1000 cu Memorie Partajata si Semafor ---\n\n";

    // 3. Creare cele 2 procese individuale
    pid_t pid1 = fork();
    if (pid1 == 0) {
        // Procesul Copil 1
        run_process(1, shared_count, sem);
        return 0;
    }

    pid_t pid2 = fork();
    if (pid2 == 0) {
        // Procesul Copil 2
        run_process(2, shared_count, sem);
        return 0;
    }

    // Procesul Părinte: Așteaptă finalizarea ambelor procese
    waitpid(pid1, NULL, NULL);
    waitpid(pid2, NULL, NULL);

    std::cout << "\n============================================\n";
    std::cout << " Numaratoare finalizata! Valoare finala memorie: " << *shared_count << "\n";
    std::cout << "============================================\n";

    // Curățare resurse sistem
    munmap(shared_count, sizeof(int));
    close(shm_fd);
    shm_unlink(SHM_NAME);

    sem_close(sem);
    sem_unlink(SEM_NAME);

    return 0;
}
