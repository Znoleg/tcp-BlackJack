#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <cstring>
#include <string>
#include <vector>
#include <map>
#include <iostream>
#include <sstream>
#include <ncurses.h>
#include "../Variables.h"

using namespace std;

WINDOW* win;
char my_username[packet_size];
int socketfd;
int my_id;

// Структура для представления координат
struct Coordinates
{
    int x, y;
};

// Структура в которой хранятся координаты игрока
struct PlayerPositions
{
    Coordinates nickname_pos;
    Coordinates cards_pos;
    Coordinates score_pos;
    Coordinates cardscore_pos;
};

const Coordinates center {35, 7}; // Координаты центра
PlayerPositions down_player; // Координаты игроков
PlayerPositions up_player;
PlayerPositions left_player;
PlayerPositions right_player;

int player_cnt;
PlayerPositions positions[4]; // Массив позиций (будем брать нужную позицию по ид)

// Пишет сообщение и завершает программу
void error_and_exit(string msg)
{
    perror(msg.c_str());
    exit(1);
}

int create_socket()
{
    int sockfd = socket(AF_INET, SOCK_STREAM, 0); // открывает сокет
    if (sockfd < 0) error_and_exit("Error opening socket!");
    return sockfd;
}

hostent* get_host_by_ip(string ipstr)
{
    in_addr ip;
    hostent *hp;
    if (!inet_aton(ipstr.c_str(), &ip)) error_and_exit("Can't parse IP address!"); // приводим текстовый ип в нужный
    if ((hp = gethostbyaddr((const void*)&ip, sizeof(ip), AF_INET)) == NULL) error_and_exit("No server with " + ipstr + " adress!"); // получаем хост по ип
    return hp;
}

sockaddr_in server_connect(in_port_t port, int sockfd, hostent* server)
{
    sockaddr_in serv_addr;
    bzero((char*)&serv_addr, sizeof(serv_addr));
    bcopy((char*)server->h_addr_list[0], (char*)&serv_addr.sin_addr.s_addr, server->h_length);
    serv_addr.sin_family = AF_INET; // TCP
    serv_addr.sin_port = htons(port);
    if (connect(sockfd, (sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) error_and_exit("Error connecting");
    return serv_addr;
}

// Пишет на заданной позиции и может принять цвет
void print_at_pos(int x, int y, string msg, int color_pair = -1)
{
    move(y, x);
    if (color_pair != -1) attron(COLOR_PAIR(color_pair));
    refresh();
    printw(msg.c_str());
    if (color_pair != -1) attron(COLOR_PAIR(1));
    refresh();
}

// Пишет на заданной позиции и может принять цвет
void print_at_pos(Coordinates coord, string msg, int color_pair = -1)
{
    ulong x = coord.x;
    ulong y = coord.y;
    print_at_pos(x, y, msg, color_pair);
}

// Коструирует позиции и рисует стартовый экран
void construct_and_draw(char* message)
{
    stringstream parser(message); // будем парсить
    parser >> player_cnt; // число пользователей
    // Читаем ид и имя игроков, конструируем позиции, рисуем экран
    for (int i = 0; i < player_cnt; i++)
    {
        int id;
        string name;
        parser >> id >> name; // ид и имя
        if (strcmp(my_username, name.c_str()) == 0) // если полученное имя равно моему
        {
            my_id = id; // то мой ид равен этому ид (нужно для получения нужной позиции в консоли)
        }
        if (i == 0) // Первый пользователь нижний
        {
            const int crds_st_x = 35;
            down_player = {{crds_st_x - (int)name.size() / 2, 11}, {crds_st_x, 12}, {crds_st_x + 1, 13}, {crds_st_x - 3, 12}};
            positions[0] = down_player;
        }
        else if (i == 1) // Второй пользователь верхний
        {
            const int crds_st_x = 35;
            up_player = {{crds_st_x - (int)name.size() / 2, 2}, {crds_st_x, 3}, {crds_st_x + 1, 4}, {crds_st_x - 3, 3}};
            positions[1] = up_player;
        }
        else if (i == 2) // Третий пользователь левый
        {
            const int crds_st_x = 5;
            left_player = {{crds_st_x - (int)name.size() / 2, 6}, {crds_st_x, 7}, {crds_st_x + 1, 8}, {crds_st_x - 3, 7}}; // do 14
            positions[2] = left_player;
        }
        else if (i == 3) // Четвёртый правый
        {
            const int crds_st_x = 67;
            right_player = {{crds_st_x - (int)name.size() / 2, 6}, {crds_st_x, 7}, {crds_st_x, 8}, {crds_st_x - 3, 7}};
            positions[3] = right_player;
        }
        
        print_at_pos(positions[i].nickname_pos, name, 2); // Пишем ник на позиции ника
        print_at_pos({positions[i].score_pos.x - 7, positions[i].score_pos.y}, "Score: 0", 3); // Очки
        print_at_pos(positions[i].cardscore_pos, "0", 4); // Очки карт
    }
}

// Пишет карту на нужной позиции
void print_card(Coordinates position, int card)
{
    if (card == 2) // валет
    {
        print_at_pos(position, "J");
    }
    else if (card == 3) // дама
    {
        print_at_pos(position, "Q");
    }
    else if (card == 4) // король
    {
        print_at_pos(position, "K");
    }
    else if (card == 11) // туз
    {
        print_at_pos(position, "A");
    }
    else
    {
        print_at_pos(position, to_string(card));
    }
}

bool printed;
int seconds_left;

void* wait_and_print(void* arg)
{
    while (seconds_left > 0 && !printed) // пока секунды больше 0 и клиент не нажал клавишу
    {
        move(center.y, center.x);
        refresh();
        printw("  "); // стираем старое число в центре
        move(center.y, center.x);
        refresh();
        printw(to_string(seconds_left).c_str()); // пишем новое, сконтвертировав в стринг
        refresh();
        sleep(1); // ждём секунду
        seconds_left--;
    }

    pthread_exit(0);
}

void* wait_for_kbhit(void* res)
{
    char* result = (char*)res;
    pthread_t thread;
    pthread_create(&thread, 0, wait_and_print, 0); // поток для написания секунд таймера

    printed = false;
    seconds_left = decision_time; // ставим время ожидания на стандартное (меняется в wait_and_print)
    keypad(stdscr, TRUE); // режим получания клавиш для гетч
    halfdelay(seconds_left * 10); // сколько гетч ждёт нажатия
    while (seconds_left > 1) // цикл пока секунды > 1 (1 остаётся из-за косячной параллельности, поэтому 1 а не 0)
    {
        char ch = getch();
        if (ch == drawcard_keycode || ch == passcard_keycode) // если нажата клавиша взятия карты или паса (пробел и enter по стандарту)
        {
            *result = ch;
            printed = true; // ставим принтед на тру (чтобы закончить вывод таймер)
            pthread_exit(0);
        }
        else if (ch != ERR)
        {
            halfdelay(seconds_left * 10); // если нажата другая клавиша то ставим гетч с обновлённым кол-вом секунд
            continue;
        }
    }
    
    pthread_exit(0);
}

// Печатает последовательно карты, берёт позицию с помощью ид
void print_cards(const int id, const vector<int>& cards)
{
    Coordinates card_pos = positions[id].cards_pos; // получаем координаты
    for (auto card : cards)
    {
        print_card(card_pos, card);
        int x, y;
        getyx(stdscr, y, x); // получаем позицию в консоли
        card_pos = {x + 1, y};  // новая позиция это старая +1 по иксу
    }
}

// Чистит экран между раундами
void reset_game_screen(int players_cnt)
{
    for (int i = 0; i < player_cnt; i++)
    {
        print_at_pos(positions[i].cardscore_pos, "  "); // чистим очки карт
        print_at_pos(positions[i].cardscore_pos, "0", 4); // пишем 0 очков карт
        print_at_pos(positions[i].cards_pos, "          "); // чистим карты
        print_at_pos({center.x - 7, center.y}, "                                      "); // чистим собщение посередине
    }
}

void* game_thread(void*)
{
    PlayerPositions my_pos = positions[my_id];
    
    while (true) // цикл игры
    {
        Coordinates next_card_pos = my_pos.cards_pos; 
        int card_score = 0;
        int card;
        recv(socketfd, &card, sizeof(int), 0);
        
        card_score += card;
        print_at_pos(my_pos.cardscore_pos, to_string(card_score), 4);
        print_card(next_card_pos, card);
        int x, y;
        getyx(win, y, x); // получаем позицию x,y после записи карты (мы не знаем карта однозначная или двузначная)
        next_card_pos = {x + 1, y}; // сохраняем след. позицию карты

        bool is_playing = true;
        while (is_playing) // цикл до тех пор пока не выбыл
        {
            char pressed = 0;
            pthread_t thread;
            pthread_create(&thread, 0, wait_for_kbhit, (void*)&pressed); // получаем нажатую кнопку
            pthread_join(thread, 0);
            
            char decision[packet_size];
            char* msg;
            if (pressed == drawcard_keycode) // если была нажата кнопка взятия карты (пробел по стандарту)
            {
                strcpy(decision, "Draw");
                send(socketfd, decision, packet_size, 0); // отсылаем "Draw"

                char* msg = "You picked a card. Wait others.";
                move(center.y, center.x - sizeof(msg) / 2);
                refresh();
                printw(msg); // Пишем сообщение
                refresh();
            }
            else // иначе
            {
                strcpy(decision, "Pass");
                send(socketfd, decision, packet_size, 0); // отсылаем "Pass"
                move(center.y, center.x);

                char* msg = "Pass! Wait others.";
                move(center.y, center.x - sizeof(msg) / 2);
                refresh();
                printw(msg); // Пишем сообщение
                refresh();

                is_playing = false; // выходим из цикла
                break;
            }

            sleep(seconds_left); // ждём оставшее кол-во секунд до новой итерации
            print_at_pos(center.x - sizeof(msg), center.y , "                                      "); // чистим сообщение в середине
            int perebor;
            recv(socketfd, &perebor, sizeof(int), 0); // получаем с сервера перебрали ли мы или нет
            
            recv(socketfd, &card, sizeof(int), 0); // получаем зарандомленную карту с сервера
            card_score += card; // прибавляем очки
            print_at_pos(my_pos.cardscore_pos, to_string(card_score), 4); // пишем очки
            print_card(next_card_pos, card); // пишем карту

            int x, y;
            getyx(win, y, x); // получаем позицию x,y после записи карты (мы не знаем карта однозначная или двузначная)
            next_card_pos = {x + 1, y}; // сохраняем след. позицию карты

            if (perebor == 1) // если перебор
            {
                move(center.y, center.x);
                refresh();
                printw("Perebor! Wait until round end."); // пишем сообщение об этом в середине экрана
                refresh();
                is_playing = false; // выходим из цикла
                break;
            }
        }

        for (int i = 0; i < player_cnt; i++) // получаем данные о реультатах всех игроков
        {
            char results[packet_size];
            recv(socketfd, results, packet_size, 0);
            
            stringstream parser(results); // будем парсить
            vector<int> cards;
            int id, card_score, score, card; 
            parser >> id >> card_score >> score; // ввод ид очков_карт очков
            while (parser.peek() != EOF) // потом идут карты то конца строки
            {
                parser >> card;
                if (card == 0) break;
                cards.push_back(card); // заполняем вектор карты текущего пользователя
            }
            print_at_pos(positions[id].score_pos, to_string(score), 3); // пишем очки

            if (id != my_id) // если ид строки не равно нашему ид
            {
                print_at_pos(positions[id].cardscore_pos, to_string(card_score), 4); // пишем очки карт
                print_cards(id, cards); // пишем карты
            }
        }

        sleep(round_delay * player_cnt); // спим время задержка*кол-во пользователей, чтобы пользователь успел проанализировать рез-ты
        char buf[packet_size];
        reset_game_screen(player_cnt); // чистим экран

        char r = 'r';
        send(socketfd, &r, sizeof(char), 0); // отсылаем серверу "флаг" готовности
    }
}

int main(int argc, char *argv[])
{
    if (argc < 3)
    {
        error_and_exit("You have to give adress:port and your name as arguments!\n");
    }

    string addr = argv[1];
    int port_index = addr.find(':');
    string ip = addr.substr(0, port_index);
    int port = atoi(addr.substr(port_index + 1, addr.size() - 1).c_str()); // разбиваем адресс на до двоеточия и после

    strcpy(my_username, argv[2]); // копируем имя
    
    setlocale(LC_ALL, "Russian");
    win = initscr(); // инициализация окна для нкурсес
    resize_term(100, 100); // большой размер консоли для нкурсес
    start_color(); // чтобы работал цвет в цонсоли

    init_pair(1, COLOR_WHITE, COLOR_BLACK); // Пары цветов для ncurses, первое - цвет текста, второе - фона
    init_pair(2, COLOR_BLUE, COLOR_BLACK);
    init_pair(3, COLOR_CYAN, COLOR_BLACK);
    init_pair(4, COLOR_YELLOW, COLOR_BLACK);
    init_pair(5, COLOR_RED, COLOR_BLACK);
    init_pair(6, COLOR_GREEN, COLOR_BLACK);

    hostent *server;
    socketfd = create_socket();
    server = get_host_by_ip(ip);
    sockaddr_in serv_addr = server_connect(port, socketfd, server);
    printf("%s\n", "Successfully connected! Waiting for game start...");

    pthread_t game_thr;
    send(socketfd, my_username, packet_size, 0); // Отправляем имя на сервер
    
    char message[packet_size * 3];
    while (recv(socketfd, message, packet_size * 3, 0) <= 0);
    construct_and_draw(message);
    

    pthread_create(&game_thr, 0, game_thread, 0);
    pthread_join(game_thr, 0);
}