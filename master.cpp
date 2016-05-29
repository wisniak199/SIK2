#include <libssh/libssh.h>

#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <cstring>
#include <thread>
#include <mutex>
#include <vector>
#include <boost/regex.hpp>
#include <poll.h>




#include <sys/types.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <ctype.h>
#include <cassert>
#include <stdlib.h>

#include <iomanip>        // std::put_time
#include <chrono>         // std::chrono::system_clock
#include <ctime>

#include "err.h"


using namespace std;

mutex m;

struct start_parameters
{
    string host;
    string radio;
    string path;
    string rport;
    string file;
    string mport;
    string md;
    int time;
};

struct at_parameters
{
    start_parameters start;
    int hh;
    int mm;
    int minutes;
};
struct player_info
{
    start_parameters param;
    int id;
    int started;
    int kill;
    //int ended;
};

int send_to_client(int sock, string msg, mutex& m)
{
    m.lock();
    cerr << "sending!" << endl;
    int len = msg.length();
    const char *buffer = msg.c_str();
    if (len <= 0)
    {
        m.unlock();
        return 0;
    }
    while (len)
    {
        int write_len = write(sock, buffer, len);
        if (write_len == -1)
        {
            m.unlock();
            return -1;
        }
        len -= write_len;
        buffer += write_len;
    }
    m.unlock();
    return msg.length();
}

//            start_player(p, telnet_sockfd, telnet_mutex, players, players_mutex, counter);

void start_player(start_parameters p, int sockfd, mutex& telnet_mutex, int id)
{
    string cmd = "ssh " + p.host +  " -o BatchMode=yes 'bash -l -c \"player " + p.radio + " " + p.path + " " + p.rport + " " + p.file + " " + p.mport + " " + p.md + "\"'";
    int res = system(cmd.c_str());
    int exit_status = WEXITSTATUS(res);
    if (exit_status != 0)
    {
        if(exit_status == 255)
            send_to_client(sockfd, "ERROR " + to_string(id) + " ERROR WITH SSH " + to_string(exit_status) + "\n", telnet_mutex);
        else
            send_to_client(sockfd, "ERROR " + to_string(id) + " PLAYER EXITED WITH CODE "+ to_string(exit_status) + "\n", telnet_mutex);

    }
}


enum command
{
    PLAY,
    PAUSE,
    TITLE,
    QUIT,
    START,
    WRONG,
    AT,
};

start_parameters get_start_parameters(string& msg)
{
    boost::regex base_regex("START\\s+([^\\s]+)\\s+([^\\s]+)\\s+([^\\s]+)\\s+([^\\s]+)\\s+([^\\s]+)\\s+([^\\s]+)\\s+([^\\s]+)");
    boost::smatch base_match;
    start_parameters p;
    if (boost::regex_search(msg, base_match, base_regex))
    {
        if (base_match.size() == 8)
        {
                boost::ssub_match base_sub_match = base_match[1];
                p.host = base_sub_match.str();

                base_sub_match = base_match[2];
                p.radio = base_sub_match.str();

                base_sub_match = base_match[3];
                p.path = base_sub_match.str();

                base_sub_match = base_match[4];
                p.rport = base_sub_match.str();

                base_sub_match = base_match[5];
                p.file = base_sub_match.str();

                base_sub_match = base_match[6];
                p.mport = base_sub_match.str();

                base_sub_match = base_match[7];
                p.md = base_sub_match.str();
        }
    }
    return p;
}


at_parameters get_at_parameters(string& msg)
{
    boost::regex base_regex("AT\\s+([0-9][0-9]):([0-9][0-9])\\s+([0-9]+)\\s+([^\\s]+)\\s+([^\\s]+)\\s+([^\\s]+)\\s+([^\\s]+)\\s+([^\\s]+)\\s+([^\\s]+)\\s+([^\\s]+)");
    boost::smatch base_match;
    at_parameters p;
    if (boost::regex_search(msg, base_match, base_regex))
    {
        if (base_match.size() == 11)
        {
                boost::ssub_match base_sub_match = base_match[1];
                p.hh = stoi(base_sub_match.str());

                base_sub_match = base_match[2];
                p.mm = stoi(base_sub_match.str());

                base_sub_match = base_match[3];
                p.minutes = stoi(base_sub_match.str());

                base_sub_match = base_match[4];
                p.start.host = base_sub_match.str();

                base_sub_match = base_match[5];
                p.start.radio = base_sub_match.str();

                base_sub_match = base_match[6];
                p.start.path = base_sub_match.str();

                base_sub_match = base_match[7];
                p.start.rport = base_sub_match.str();

                base_sub_match = base_match[8];
                p.start.file = base_sub_match.str();

                base_sub_match = base_match[9];
                p.start.mport = base_sub_match.str();

                base_sub_match = base_match[10];
                p.start.md = base_sub_match.str();
        }
    }
    return p;
}

int get_id(string& msg, command c)
{
    string com;
    switch(c)
    {
        case PLAY:
            com = "PLAY";
            break;
        case PAUSE:
            com = "PAUSE";
            break;
        case QUIT:
            com = "QUIT";
            break;
        case TITLE:
            com = "TITLE";
            break;
        default:
            //impossible
            break;
    }

    boost::regex base_regex("\\s*" + com + "\\s+([0-9]+)\\s+");
    boost::smatch base_match;
    int id = -1;
    if (boost::regex_search(msg, base_match, base_regex))
    {
        if (base_match.size() == 2)
        {
            boost::ssub_match base_sub_match = base_match[1];
            id = stoi(base_sub_match.str());
        }
    }
    return id;
}

//send_control_msg(c, find_res->second, telnet_sockfd, telnet_mutex, players, players_mutex);
void send_control_msg(command c, player_info info, int sockfd, mutex& sock_mutex, map<int, player_info>& players, mutex& players_mutex, int silent)
{
    struct addrinfo hints, *res;
    int control_sockfd;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;

    if (getaddrinfo(info.param.host.c_str(), info.param.mport.c_str(), &hints, &res) != 0)
    {
        cerr << "getaddrinfo" << endl;
        if (!silent)
            send_to_client(sockfd, "NOT OK " + to_string(info.id) + "\n", sock_mutex);
        return;
    }
    control_sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if(control_sockfd == -1)
    {
        if (!silent)
            send_to_client(sockfd, "NOT OK " + to_string(info.id) + "\n", sock_mutex);
        cerr << "socket" << endl;
        return;
    }

    struct sockaddr_in my_address;
    my_address.sin_family = AF_INET; // IPv4
    my_address.sin_addr.s_addr =
        ((struct sockaddr_in*) (res->ai_addr))->sin_addr.s_addr; // address IP
    my_address.sin_port = htons((uint16_t) atoi(info.param.mport.c_str()));

    string msg;
    switch(c)
    {
        case PLAY:
            msg = "PLAY";
            break;
        case PAUSE:
            msg = "PAUSE";
            break;
        case QUIT:
            msg = "QUIT";
            break;
        case TITLE:
            msg = "TITLE";
            break;
        default:
            //impossible
            break;
    }
    if (sendto(control_sockfd, msg.c_str(), msg.length(), 0, (struct sockaddr *) &my_address, sizeof(my_address)) == -1)
    {
        cerr << "sendto" << endl;
        if (!silent)
            send_to_client(sockfd, "NOT OK " + to_string(info.id) + "\n", sock_mutex);
        return;
    }


    if (c == TITLE)
    {
        struct pollfd fd;
        fd.fd = control_sockfd;
        fd.events = POLLIN;
        switch(poll(&fd, 1, 3000))
        {
            case -1:
            case 0:
            {
                //string msg = "NOT OK " + to_string(info.id) + "\n";
                if (!silent)
                    send_to_client(sockfd, "NOT OK " + to_string(info.id) + "\n", sock_mutex);
                break;
            }
            default:
            {
                char buffer[200];
                int read_len = read(control_sockfd, buffer, sizeof(buffer));
                if (read_len <= 0)
                {
                    if (!silent)
                        send_to_client(sockfd, "NOT OK " + to_string(info.id) + "\n", sock_mutex);
                }
                //string msg = "OK " + to_string(info.id) + " " + string(buffer, read_len) + "\n";
                if (!silent)
                    send_to_client(sockfd, "OK " + to_string(info.id) + " " + string(buffer, read_len) + "\n", sock_mutex);
                break;
            }
        }
    }
    else
    {
        //string msg = "OK " + to_string(info.id) + "\n";
        if (!silent)
            send_to_client(sockfd, "OK " + to_string(info.id) + "\n", sock_mutex);
    }
}

command command_type(string& msg)
{
    map<command, string> coms = {{PLAY, "PLAY"}, {PAUSE, "PAUSE"}, {TITLE, "TITLE"}, {QUIT, "QUIT"}};
    for (auto& c : coms)
    {
        boost::regex base_regex("\\s*" + c.second + "\\s+([0-9]+)\\s+");
        boost::smatch base_match;
        if (boost::regex_search(msg, base_match, base_regex))
            return c.first;
    }

    boost::regex base_regex("START\\s+([^\\s]+)\\s+([^\\s]+)\\s+([^\\s]+)\\s+([^\\s]+)\\s+([^\\s]+)\\s+([^\\s]+)\\s+([^\\s]+)");
    boost::smatch base_match;
    if (boost::regex_search(msg, base_match, base_regex))
        return START;

    base_regex = boost::regex("AT\\s+([0-9][0-9]:[0-9][0-9])\\s+([0-9]+)\\s+([^\\s]+)\\s+([^\\s]+)\\s+([^\\s]+)\\s+([^\\s]+)\\s+([^\\s]+)\\s+([^\\s]+)\\s+([^\\s]+)");
    if (boost::regex_search(msg, base_match, base_regex))
        return AT;
    return WRONG;
}

int read_next_message(string& msg, int sockfd, mutex& telnet_mutex)
{
    char buffer[200];
    //telnet_mutex.lock();
    int res = read(sockfd, buffer, sizeof(buffer));
    //telnet_mutex.unlock();
    if (res <= 0)
        return res;
    msg = string(buffer, res);
    return res;
}



void set_time(struct std::tm *ptm, int hh, int mm)
{
    if (!(ptm->tm_hour < hh || (ptm->tm_hour == hh && ptm->tm_min < mm)))
    {
        ptm->tm_mday++;
        mktime(ptm);
    }
    ptm->tm_hour = hh;
    ptm->tm_min = mm;
    ptm->tm_sec = 0;
}

void start_player_delayed(at_parameters p, int sockfd, mutex& telnet_mutex, map<int, player_info>& players, mutex& players_mutex, int id)
{
    //najpierw czekam do godziny
    // odpalam watek ze startem playera
    // potem czekam iles minut
    //wysylam mu quit bez wzgledu czy dziala
    // join z watkiem
    // koniec
    using std::chrono::system_clock;
    std::time_t tt = system_clock::to_time_t (system_clock::now());
    struct std::tm * ptm = std::localtime(&tt);
    set_time(ptm, p.hh, p.mm);
    std::this_thread::sleep_until (system_clock::from_time_t (mktime(ptm)));

    players_mutex.lock();
    if (players[id].kill == 1)
    {
        players_mutex.unlock();
        return;
    }

    players[id].started = 1;
    players_mutex.unlock();

    thread t(start_player, p.start, sockfd, ref(telnet_mutex), id);

    std::this_thread::sleep_for(std::chrono::minutes(p.minutes));

    // trzeba sprawdzic czy nikt go wczesniej nie wylaczyl
    players_mutex.lock();
    if (players[id].kill == 1)
    {
        players_mutex.unlock();
        return;
    }
    players[id].kill = 1;
    player_info info = players[id];
    players_mutex.unlock();

    send_control_msg(QUIT, info, sockfd, telnet_mutex, players, players_mutex, 1);

    t.join();
}

bool check_clock(int hh, int mm)
{
    return hh >= 0 && hh < 24 && mm >= 0 && mm < 60;
}



void process_message(string msg, int telnet_sockfd, mutex& telnet_mutex, map<int, player_info>& players, mutex& players_mutex, int* counter)
{
    string username = "wisniak199";
    cerr << "przed commandtype " << msg << endl;
    //sleep(1000);
    command c = command_type(msg);
    cerr << "przed switchem" << endl;
    //sleep(1000);
    //c = WRONG;
    switch(c)
    {
        case AT:
        {
            cerr << "in at" << endl;
            at_parameters p = get_at_parameters(msg);
            if (p.start.host != "" && check_clock(p.hh, p.mm))
            {
                players_mutex.lock();
                int id = *counter;
                *counter += 1;
                player_info info;
                info.started = 0;
                info.kill = 0;
                info.id = id;
                info.param = p.start;
                players[id] = info;
                players_mutex.unlock();

                send_to_client(telnet_sockfd, "OK " + to_string(id) + "\n", telnet_mutex);
                start_player_delayed(p, telnet_sockfd, telnet_mutex, players, players_mutex, id);


                players_mutex.lock();
                players.erase(id);
                players_mutex.unlock();
            }
            else
            {
                send_to_client(telnet_sockfd, "Wrong command AT\n", telnet_mutex);
            }
            break;
        }
        case START:
        {
            cerr << "in start" << endl;
            start_parameters p = get_start_parameters(msg);
            if (p.host != "")
            {

                players_mutex.lock();
                int id = *counter;
                *counter += 1;
                player_info info;
                info.started = 1;
                info.kill = 0;
                info.id = id;
                info.param = p;
                players[id] = info;
                players_mutex.unlock();

                send_to_client(telnet_sockfd, "OK " + to_string(id) + "\n", telnet_mutex);
                start_player(p, telnet_sockfd, telnet_mutex, id);

                players_mutex.lock();
                players.erase(id);
                players_mutex.unlock();
            }
            else
            {
                send_to_client(telnet_sockfd, "Wrong command START\n", telnet_mutex);
            }
            break;
        }
        case WRONG:
        {
            cerr << "in wrong" << endl;
            cerr << "after lock" << endl;
            if (send_to_client(telnet_sockfd, "Wrong command\n", telnet_mutex) == -1) cerr << "error in send to client" << endl;
            break;
        }
        case QUIT:
        {
            cerr << "in quit" << endl;
            int id = get_id(msg, c);
            players_mutex.lock();
            auto find_res = players.find(id);
            player_info info;
            int found = 0;
            if (find_res != players.end())
            {
                found = 1;
                info = find_res->second;
                find_res->second.kill = 1;
            }
            players_mutex.unlock();

            if (found && info.kill == 0)
            {
                if (info.started == 1)
                {
                    send_control_msg(c, info, telnet_sockfd, telnet_mutex, players, players_mutex, 0);
                }
                else
                {
                    send_to_client(telnet_sockfd, "OK " + to_string(id) + "\n", telnet_mutex);
                }
            }
            else
            {
                send_to_client(telnet_sockfd, "UNKNOWN ID\n", telnet_mutex);
            }
            break;
        }
        default:
        {
            cerr << "in default" << endl;
            int id = get_id(msg, c);
            players_mutex.lock();
            auto find_res = players.find(id);
            player_info info;
            int found = 0;
            if (find_res != players.end())
            {
                found = 1;
                info = find_res->second;
            }
            players_mutex.unlock();

            //obsluzyc ze do wystartowanego z opoznieniem mozna wyslac tylko quit
            if (found && info.kill == 0 && info.started == 1)
            {
                    send_control_msg(c, info, telnet_sockfd, telnet_mutex, players, players_mutex, 0);
            }
            else
            {
                send_to_client(telnet_sockfd, "UNKNOWN ID\n", telnet_mutex);
            }

        }
    }
}


void telnet_session(int sockfd)
{
    vector<thread> commands;
    mutex players_mutex, telnet_mutex;
    map<int, player_info> players;
    int counter = 1;
    string msg;
    int res;
    while (true)
    {
        msg = "";
        res = read_next_message(msg, sockfd, telnet_mutex);
        if (res == 0)
        {
            cerr << "res = 0" << endl;
            break;

        }
        else if (res == -1)
        {
            cerr << "res = -1" << endl;
            break;
        }
        cerr << "odpalanie threada" << endl;
        thread t(process_message, msg, sockfd, ref(telnet_mutex), ref(players), ref(players_mutex), &counter);
        commands.push_back(move(t));
    }

    for (auto& c : commands)
        c.join();
    close(sockfd);
    //cerr << "zamkniece telneta";
    return;

}



int main(int argc, char* argv[])
{

    // gadanie z telnetem
    vector<thread> telnet_sessions;
    char *PORT_NUM = "50001";
    if (argc == 2)
        PORT_NUM = argv[1];
    else if (argc > 2)
    {
        fprintf(stderr, "Usage: %s [port]\n", argv[0]);
        return 1;
    }

    struct sockaddr_storage their_addr;
    socklen_t addr_size;
    struct addrinfo hints, *res;
    int accept_fd, new_fd;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;  // IPv4 lub IPv6
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    if (getaddrinfo(NULL, PORT_NUM, &hints, &res) != 0)
        syserr("getaddrinfo");


    accept_fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if(accept_fd == -1)
        syserr("socket");
    if (bind(accept_fd, res->ai_addr, res->ai_addrlen) == -1)
        syserr("bind");
    if (listen(accept_fd, 10) == -1)
        syserr("listen");

    while (true)
    {
        new_fd = accept(accept_fd, (struct sockaddr *)&their_addr, &addr_size);
        if (new_fd == -1) syserr("accept");
        thread t(telnet_session, new_fd);
        //t.detach();
        telnet_sessions.push_back(move(t));

    }



    for (auto& s : telnet_sessions)
        s.join();
    close(accept_fd);
    //string msg = "AT 111:55 5 localhost ant-waw-01.cdn.eurozet.pl / 8602 test5.mp3 50000 yes";
    //assert(command_type(msg) == AT);

    //printf("%s", ssh_version(0));
    /*string msg = "START              stream3.polskieradio.pl         /    8900  - 10000 no";
    start_parameters p =  get_start_parameters(msg);
    cout << p.host << '\n' << p.path << '\n' << p.rport << '\n' << p.file << '\n' << p.mport << '\n' << p.md << '\n' << endl;
*/
    /*telnet_session(5, "wisniak199");
    while(true);*/

    /*using std::chrono::system_clock;
    std::time_t tt = system_clock::to_time_t (system_clock::now());
    struct std::tm * ptm = std::localtime(&tt);
    cout << ptm->tm_hour << ":" <<ptm->tm_min <<":" <<ptm->tm_sec;*/
}

