#define T_SIZE 4
#define LER_SIZE 30
#define BUF_SIZE 4096
#include<sys/stat.h>
#include<cstring>
#include<atomic>
#include<sys/resource.h>
#include<signal.h>
#include<vector>
#include<sstream>
#include<sys/inotify.h>
#include<string>
#include<iostream>
#include<cstdlib>
#include<unistd.h>
#include<fcntl.h>
#include<sys/types.h>
#include<sys/wait.h>
#include<thread>
#include<mutex>
#include<condition_variable>
#include<sys/mman.h>
#include<queue>
#include<chrono>

std::atomic<bool> play(true);
std::mutex ler;

int p[2];
std::chrono::time_point<std::chrono::high_resolution_clock> inicio;

void print(int who, const char* mensagem) {
    auto fim = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> duracao = fim - inicio;
    struct rusage usage;
    getrusage(who, &usage);
    //RSS aqui seria o pico que o programa atingiu na memoria em KB
    std::cout << mensagem << "Tempo total do programa: " << duracao.count() << "s | " << "RSS: " << usage.ru_maxrss << "kB\n\n";
}

void sig(int sigid) {
    play = false;
    std::cout << "Programa encerrado.\n";
}

void criarPastas() {
    //comando da biblioteca stat.h para criar arquivos, ja que open apenas cria arquivos regulares, usei 0755 para permissões(escrita, leitura, exec) 
    mkdir("SAFE", 0755);    
    mkdir("PERIGOSO", 0755);
}

class leituraArquivos {
    private:
    std::mutex mtx;
    std::queue<std::string> f;
    std::condition_variable cv_p, cv_c;
    size_t max;
    public:
    leituraArquivos(size_t m) : max(m) {}

    void push(std::string item) {
        std::unique_lock<std::mutex> lock(mtx);
        cv_p.wait(lock, [this](){return f.size() < max; });
        const char* i = item.c_str();
        f.push(i);
        cv_c.notify_one();
    }

    std::string pop() {
        std::unique_lock<std::mutex> lock(mtx);
        cv_c.wait(lock, [this](){return !f.empty(); });
        std::string arquivo = f.front();
        f.pop();
        cv_p.notify_one();
        return arquivo;
    }
};

leituraArquivos l(LER_SIZE);

void checarArquivos() {
    close(p[0]);
    while (play) {
        std::string arquivo = l.pop(); 
        if (arquivo == "SAIR") { break; }
        int fd = open(arquivo.c_str(), O_RDONLY);
        if (fd < 0) { perror("open_checar"); exit(EXIT_FAILURE); }

        unsigned char buffer[4];
        ssize_t bytes_lidos = read(fd, buffer, 4);
        close(fd);

        if (bytes_lidos < 4) { std::cout << "Arquivo muito pequeno"; }
        bool perigoso = false;
        //isso aqui vaiu checar se ele é um executavel do linux ou não a partir dos 4 bytes extraidos no formato hexadecimal
        if (buffer[0] == 0x7F && buffer[1] == 0x45 && buffer[2] == 0x4C && buffer[3] == 0x46) {
            perigoso = true;
        } /*agora iremos checar para o windows */ else if (buffer[0] == 'M' && buffer[1] == 'Z') {
            perigoso = true;
        } /*agora para se é um script shell (.sh)*/else if (buffer[0] == '#' && buffer[1] == '!') { perigoso = true; }

        if (perigoso) {
            std::cout << "log> arquivo perigoso detectado: " << arquivo << '\n';
            std::string txt_perigo = "PERIGOSO:" + arquivo + "\n";
            write(p[1], txt_perigo.c_str(), txt_perigo.length());
        } else { 
            std::cout << "log> arquivo analisado como seguro: " << arquivo << '\n';
            std::string txt_safe = "SAFE:" + arquivo + "\n";
            write(p[1], txt_safe.c_str(), txt_safe.length());
        }
        usleep(1000);
    }
    const char* fim = "SAIR\n";
    write(p[1], fim, strlen(fim));
}

void checarArquivosNovos(std::string path) {
    int fd = inotify_init();
    if (fd < 0) { perror("inofity"); exit(EXIT_FAILURE); }
    int verificar = inotify_add_watch(fd, path.c_str(), IN_CLOSE_WRITE);
    char buffer[BUF_SIZE];
    while (play) {
        int tamanho = read(fd, buffer, BUF_SIZE);
        int i = 0;
        while (i < tamanho) {
            //para o inotify vizualizar os arquivos com a estrutura dele como se faz igual no getdents64 com a struct "linux_dents64"
            struct inotify_event* event = (struct inotify_event*)&buffer[i];
            if (event->len) { 
                std::string nome_arquivo = event->name; 
                if (nome_arquivo != "SAFE" && nome_arquivo != "PERIGOSO") {
                    std::cout << "log> novo arquivo detectado no diretorio atual: " << nome_arquivo << '\n';
                    l.push(nome_arquivo);
                }
            } 
            i += sizeof(struct inotify_event) + event->len;
        }
    }
    //encerrar o scraping de novos arquivos
    inotify_rm_watch(fd, verificar);
    close(fd);
}

int main() {
    inicio = std::chrono::high_resolution_clock::now();
    signal(SIGINT, sig);
    pipe(p);
    pid_t pid = fork();
    if (pid < 0) { perror("fork1"); exit(EXIT_FAILURE); }
    if (pid == 0) {
        close(p[1]);
        while (true) {
            char buffer[BUF_SIZE] = {0};
            int r = read(p[0], buffer, sizeof(buffer) - 1);
            
            if (r <= 0) { break; }
            buffer[r] = '\0';

            char* linha = strtok(buffer, "\n"); //para separar o texto do "\n"
            while (linha != NULL) { //enquanto o texto sem \n ainda estiver ele executa

                if (strcmp(linha, "SAIR") == 0) { break; } 
                criarPastas();
            
                char* nome_sem_prefixo = strchr(linha, ':');
                if (nome_sem_prefixo == NULL) { linha = strtok(NULL, "\n"); continue; }
                nome_sem_prefixo++;
                if (linha[0] == 'P') { 
                    //para verificar se o arquivo existe ou nao no diretorio atual para nao conflitar
                    if (access(nome_sem_prefixo, F_OK) == 0) {
                        std::cout << "movendo arquivo '" << nome_sem_prefixo << "' para pasta PERIGOSO'\n";
                        pid_t cmd = fork();
                        if (cmd < 0) { perror("cmd_DANGEROUS"); exit(EXIT_FAILURE); }
                        if (cmd == 0) {
                            close(p[0]);
                            close(p[1]);
                            execlp("mv", "mv", nome_sem_prefixo, "PERIGOSO", NULL);
                            exit(EXIT_FAILURE);
                        } else { waitpid(cmd, NULL, 0); }
                    }
                } else if (linha[0] == 'S') {
                    if (access(nome_sem_prefixo, F_OK) == 0) {
                        std::cout << "movendo arquivo '" << nome_sem_prefixo << "' para pasta SAFE'\n";
                        pid_t cmd = fork();
                        if (cmd < 0) { perror("cmd_SAFE"); exit(EXIT_FAILURE); }
                        if (cmd == 0) { 
                            close(p[0]);
                            close(p[1]);
                            execlp("mv", "mv", nome_sem_prefixo, "SAFE", NULL);
                            exit(EXIT_FAILURE);
                        } else { waitpid(cmd, NULL, 0); }
                    }
                } 
                linha = strtok(NULL, "\n");
            }
        }
        exit(EXIT_SUCCESS);
    }

    std::string path = ".";
    std::thread checar_novos(checarArquivosNovos, path);
    std::vector<std::thread> checar_arquivos;
    for (int i = 0; i < T_SIZE; i++) {
        checar_arquivos.emplace_back(checarArquivos);
    }
    if (checar_novos.joinable()) { checar_novos.join(); }

    for (int i = 0; i < T_SIZE; i++) {
        l.push("SAIR");
    }

    for (auto& t : checar_arquivos) {
        if (t.joinable()) { t.join(); }
    }
    
    usleep(2000); //tempo para parecer realista
    print(RUSAGE_SELF, "\nEstatísticas geradas:\n");
    return EXIT_SUCCESS;
}
