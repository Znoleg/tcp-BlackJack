#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <threads.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>
#include <vector>
#include <time.h>
#include <map>
#include <string>
#include "../Variables.h"

using namespace std;

const vector<int> possible_cards {2, 3, 4, 6, 7, 8, 9, 10, 11}; // возможные карты, 2 валет 3 дама 4 король 11 туз
const int max_one_card = 4; // каждой карты макс 4
vector<int> deck; // колода
bool game_started = false;
int sockfd; // сокет сервера

struct PlayerInfo
{
    int socket;
    int id;
    vector<int> cards;
    int card_score = 0;
    int score = 0;
    char* username;

    PlayerInfo(int sock, int ID, string name)
    {
        socket = sock;
        id = ID;
        username = new char[name.size()];
        strcpy(username, name.c_str());
    }
};

vector<PlayerInfo> players; // Вектор информации об игроках

// Написать и выйти
void error_and_exit(string msg)
{
    perror(msg.c_str());
    exit(1);
}

int create_socket()
{
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) error_and_exit("Error opening socket!");
    return sockfd;
}

sockaddr_in create_connection(in_port_t& port, int sockfd)
{
    sockaddr_in serv_addr;
    bzero((char*)&serv_addr, sizeof(serv_addr)); // сбрасываем переменную адреса сервера (при создании в ней есть лишняя инфа)
    serv_addr.sin_addr.s_addr = INADDR_ANY; // конструируем инфу о сервере для подключения
    serv_addr.sin_family = AF_INET; // TCP
    serv_addr.sin_port = htons(port); // порт

    int retries = 0;
    while (bind(sockfd, (sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) // пока не получится забиндить
    {
        printf("Error on binding on %i port. Chaning port.\n", port);
        port += 10; // меняем занятый порт
        serv_addr.sin_port = htons(port);
        if (retries == 10) error_and_exit("Failture 10 times. Check your ports.\n"); // если не получилость 10 раз то выходим
        retries++;
    }
    return serv_addr;
}

// Позволяет подключаться клиентам (5 максимум)
void start_listening(int sockfd)
{
    if (listen(sockfd, 5) < 0) error_and_exit("Error on listening start");
}

// Получить рандомную карту
int get_random_card()
{
    int index = rand() % (deck.size() - 1); // рандомим индекс от 0 до макс размера деки (-1 ибо индексация с нуля)
    int card = deck[index]; // карта из деки по полученному индексу
    deck.erase(deck.begin() + index); // убираем этот елемент из колоды
    return card;
}

// Обновление колоды
void deck_refresh()
{
    deck.clear(); // Польностью счищаем колоду
    for (int i = 0; i < max_one_card; i++) // Заполняем колоду возможными картами по 4 раза (каждой карты 4 штуки)
    {
        for (int i = 0; i < possible_cards.size(); i++)
        {
            deck.push_back(possible_cards[i]);
        }
    }
}

// оповестить всех клиентов сообщением
void notify_all_str(string notify)
{
    char buff[packet_size];
    strcpy(buff, notify.c_str());
    for (int i = 0; i < players.size(); i++)
    {
        send(players[i].socket, buff, sizeof(buff) * sizeof(char), 0);
    }
}

// Рестарт раунда
void round_reset()
{
    for (int i = 0; i < players.size(); i++) // чистим вектор карт всех игроков и ставим очки карт на 0
    {
        players[i].cards.clear();
        players[i].card_score = 0;
    }
    deck_refresh(); // обновляем колоду
}

void* game_thread(void*)
{
    string info; // начальная информация для игроков
    char infobuf[packet_size * 3];
    info += to_string(players.size()); // в инфу кол-во игроков

    for (int i = 0; i < players.size(); i++)
    {
        info += ' ' + to_string(players[i].id) + ' ' + players[i].username; // в инфу ид и ник игрока
    }
    strcpy(infobuf, info.c_str()); // копируем инфу в Сшный буфер

    for (int i = 0; i < players.size(); i++)
    {
        send(players[i].socket, infobuf, packet_size * 3, 0); // отправляем
    }

    while (true) // цикл игры
    {
        int playing_players = players.size(); // число игроков
        bool is_playing[playing_players]; // массив для определения не выбыл ли игрок (пас или перебор)

        for (int i = 0; i < playing_players; i++) // ставим значения массива на true
        {
            is_playing[i] = true;
        }

        printf("%s\n", "Round start.");

        for (int i = 0; i < playing_players; i++) // отправляем всем первую карту
        {
            int card = get_random_card(); // получаем карту
            players[i].cards.push_back(card); // заносим в вектор карты игрока
            players[i].card_score += card; // прибавляем игроку очки карт
            send(players[i].socket, &card, sizeof(int), 0); // отправляем карту
        }

        while (playing_players != 0) // до тех пор пока играют игроки
        {
            sleep(decision_time); // Ждём время на решение
            for (int i = 0; i < players.size(); i++) // Обрабатываем результаты всех игроков
            {
                if (!is_playing[i]) continue; // если игрок закончил играть то продолжаем цикл для другого
                PlayerInfo& player = players[i];
                char decision[packet_size];
                recv(player.socket, decision, packet_size, 0); // получаем решение игрока

                if (strcmp(decision, "Pass") == 0) // если пасует
                {
                    is_playing[i] = false; // ставим что не играет
                    playing_players--;
                    printf("Player %i pass. End playing.\n", i);
                }
                else if (strcmp(decision, "Draw") == 0) // если берёт
                {
                    int card = get_random_card(); // получаем карту и прибавляем очки
                    player.cards.push_back(card);
                    player.card_score += card;
                    int perebor = 0;

                    if (player.card_score > 21) // если перебор
                    {
                        perebor = 1; // ставим на 1
                        is_playing[i] = false; // не играет
                        playing_players--;
                        printf("Player %i perebor. End playing.\n", i);
                    }
                    send(player.socket, &perebor, sizeof(int), 0); // Отправляем результат перебора
                    send(player.socket, &card, sizeof(int), 0); // Отправляем карту
                }
                else // ошибка
                {

                }
            }
        }

        printf("Gathering endgame information\n");
        int max_cardscore = -1;
        int maxscore_index = -1;
        for (int i = 0; i < players.size(); i++) // определяем победителя
        {
            if (players[i].card_score <= 21 && players[i].card_score > max_cardscore) // если очки игрока больше текущего максимума и до 22
            {
                max_cardscore = players[i].card_score;
                maxscore_index = i; // индекс выигрышного
            }
        }

        if (maxscore_index != -1) players[maxscore_index].score += 1; // если есть победитель (а нет его если все перебрали), то увеличиваем очки на 1

        string infos[players.size()]; // результирующая информация о каждом игроке
        for (int i = 0; i < players.size(); i++) // Составляем результаты в виде "id card_score score cards..." 
        {
            infos[i] += to_string(players[i].id) + ' ' + to_string(players[i].card_score) + ' ' + to_string(players[i].score);
            for (auto card : players[i].cards)
                infos[i] += ' ' + to_string(card);
        }

        char infobuf[packet_size];
        for (int i = 0; i < players.size(); i++) // Отправляем результаты всем клиентам (для каждого клиента)
        {
            for (int j = 0; j < players.size(); j++) // (Каждая инфа)
            {
                strcpy(infobuf, infos[j].c_str());
                send(players[i].socket, infobuf, packet_size, 0);
                printf("Sended to %i info: %s\n", players[i].id, infobuf);
            }
            printf("\n");
        }

        round_reset(); // ресет раунда

        printf("Waiting for ready flags\n");
        for (int i = 0; i < players.size(); i++) // получаем флаги готовности от всех клиентов и только после этого новый раунд
        {
            char buff;
            recv(players[i].socket, &buff, sizeof(char), 0);
            printf("%i is ready\n", i);
        }
    }
}

// Поток для команд сервера
void* server_cmds_thr(void*)
{
    char command[32];
        while (true)
        {
            scanf("%s", command);
            
            if (strcmp(command, "-help") == 0) // если ввели -help
            {
                printf("%s\n%s\n%s\n", "-help   Shows server commands", "-startgame   Starts the game (if at least 2 players connected)", "-exit   Exits the server");
                printf("\n");
            }
            else if (strcmp(command, "-startgame") == 0)
            {
                if (players.size() < 1)
                    printf("%s\n", "Not enough players connected! Need at least 2");
                else
                {
                    deck_refresh();
                    pthread_t thread;
                    pthread_create(&thread, 0, game_thread, 0);
                    game_started = true;
                }
            }
            else if (strcmp(command, "-exit") == 0)
            {
                exit(1);
            }
            else
            {
                printf("Unknown command! Type -help to get help\n");
            }
        }
}

// Поток для получения клиентов
void* get_clients(void* arg)
{
    int clientId = 0;
    sockaddr_in cli_addr = *(sockaddr_in*)arg;
    socklen_t clilen = sizeof(cli_addr);
    
    while (!game_started) // Пока игра не началась (а начнётся после команды -startgame)
    {
        int client_sock = accept(sockfd, (sockaddr*)&cli_addr, &clilen); // Принимает клиента (сам стопится до тех пор пока не примет)
        if (client_sock < 0) error_and_exit("Error on accept\n");
        printf("New client connected! Fd: %i Id: %i\n", client_sock, clientId); // Пишем инфу о клиенте 

        char name[32];
        while (recv(client_sock, name, packet_size, 0) <= 0); // получаем имя (ник)
        string namestr = name;
        PlayerInfo new_player(client_sock, clientId, namestr); 
        players.push_back(new_player); // Заполняем нового игрока в вектор
        
        clientId++; // Ид++
    }
    
    pthread_exit(0); // Завершаем когда началась игра
}

int main(int argc, char* argv[])
{
    if (argc < 2)
    {
        error_and_exit("Don't forget to provide port as argument!\n");
    }
    
    sockaddr_in serv_addr, cli_addr;
    in_port_t portno;
    portno = atoi(argv[1]);
    sockfd = create_socket(); // создаём сокет
    create_connection(portno, sockfd); // создаём подключение
    start_listening(sockfd); // ползволяем клиентам подключаться

    printf("Server started. Listeing on port %i\n", portno);

    srand(time(NULL));
    pthread_t server_thr, get_client_thr;
    pthread_create(&server_thr, 0, server_cmds_thr, 0); // создаём поток для обработки команд сервера
    pthread_create(&get_client_thr, 0, get_clients, (void**)&cli_addr); // создаём поток поулчения клиентов
    pthread_join(server_thr, 0); // присоединяемся к потоку обработки команд сервера (чтобы программа не завершилась)
    
}
