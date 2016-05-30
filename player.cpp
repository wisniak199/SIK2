#include <string>
#include <iostream>
#include <fstream>
#include <cstring>
#include <cstdio>
#include <cassert>
#include <boost/regex.hpp>

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <poll.h>
#include <netinet/in.h>
//#include <stdint.h>

#include "err.h"

using namespace std;

#define BACKLOG 10

#define MAX_HEADER_SIZE 10000
#define MAX_RADIO_BUFFER_SIZE 50000
#define MAX_METADATA_BUFFER_SIZE 5000
#define MAX_CONTROL_BUFFER_SIZE 1024

// struktury do trzymania roznych czzesci streamu
typedef struct
{
    char buffer[MAX_HEADER_SIZE];
    int length;
} header_stream;

typedef struct
{
    char buffer[MAX_RADIO_BUFFER_SIZE];
    int length;
} radio_stream;

typedef struct
{
    char buffer[MAX_METADATA_BUFFER_SIZE];
    int length;
} metadata_stream;

typedef struct
{
    char buffer[MAX_CONTROL_BUFFER_SIZE];
    int length;
} control_stream;

radio_stream stream;
metadata_stream metadata;
header_stream header;
control_stream control;
char control_buffer[MAX_METADATA_BUFFER_SIZE];
int metaint = 0;


void read_metaint(string& header)
{
    boost::regex base_regex("icy-metaint\\:([0-9]+)");
    boost::smatch base_match;
    if (boost::regex_search(header, base_match, base_regex))
    {
        if (base_match.size() == 2)
        {
                boost::ssub_match base_sub_match = base_match[1];
                string metaint_str = base_sub_match.str();
                metaint = stoi(metaint_str);
        }
    }
}



int write_to_radio_stream(radio_stream* stream, char* data, int length)
{
    if (stream->length + length > MAX_RADIO_BUFFER_SIZE)
    {
        cerr << length << endl;
        return -1;
    }
    for (int i = 0; i < length; ++i)
        stream->buffer[stream->length + i] = data[i];
    stream->length += length;
    return length;

}


string create_header(char* host, char* port, char* path, int meta)
{
    string header = "GET / HTTP/1.0\r\nHost: " + string(host) + ":" + string(port) +
        "\r\nUser-Agent: MPlayer 2.0-728-g2c378c7-4build1\r\nAccept: *" + string(path) + "*\r\nIcy-MetaData:" +
        to_string(meta) + "\r\nConnection: close\r\n\r\n";
    return header;
}


ssize_t send_all(int socket, const void *buffer, size_t length, int flags)
{
    ssize_t len = length;
    char *ptr = (char*) buffer;
    while (length > 0)
    {
        ssize_t i = send(socket, ptr, length, flags);
        if (i < 1) return i;
        ptr += i;
        length -= i;
    }
    return len;
}


bool is_header_ok(string& header, int length)
{
    boost::regex base_regex("ICY 200 OK\r\n");
    boost::smatch base_match;
    if (boost::regex_search(header, base_match, base_regex))
        return true;
    return false;
}


// wsylanie requesta do serwera radia
void send_request(int sockfd, const string& request)
{
    if (send_all(sockfd, request.c_str(), request.length(), 0) == -1)
        syserr("send");
}


// wrappery na read uzywajace struktur
int read_radio_stream(int sockfd, radio_stream *stream, int size)
{
    int read_len = read(sockfd, stream->buffer + stream->length, size);
    if (read_len == -1) syserr("read1");
    stream->length += read_len;
    return read_len;
}

int read_header_stream(int sockfd, header_stream *stream, int size)
{
    int read_len = read(sockfd, stream->buffer + stream->length, size);
    if (read_len == -1) syserr("read2");
    stream->length += read_len;
    return read_len;
}

int read_metadata_stream(int sockfd, metadata_stream *stream, int size)
{
    int read_len = read(sockfd, stream->buffer + stream->length, size);
    if (read_len == -1) syserr("read3");
    stream->length += read_len;
    return read_len;
}


//sprawdza czy w streame wystapil juz koniec headera
int check_header_end(header_stream *stream, int start, int length)
{
    if (start < 0 || start + length > stream->length)
    {
        return -1;
    }
    string chunk(stream->buffer + start, length);
    size_t found = chunk.find("\r\n\r\n");
    if (found == string::npos)
        return -1;
    return start + (int)found;
}



int read_response(int radio_sockfd, header_stream *stream)
{
    int read_len = 0;
    struct pollfd poll_tab[1];
    poll_tab[0].fd = radio_sockfd;
    poll_tab[0].events = POLLIN;

    // timeout na 5s
    switch(poll(poll_tab, 1, 5000))
    {
        case -1:
            syserr("poll");
        case 0:
            cerr << "radio stream didnt respond" << endl;
            exit(1);
        default:
            break;
    }

    // czytamy naglowek odpowiedzi az nie spotkamy \r\n\r\n, jezeli wczytalismy troche za duzo
    // bajtow to zostana one przepisane
    while(stream->length < MAX_HEADER_SIZE)
    {
        read_len = read_header_stream(radio_sockfd, stream, MAX_HEADER_SIZE - stream->length);
        if (stream->length - read_len > 3)
        {
            int header_end = check_header_end(stream, stream->length - read_len - 3, read_len + 3);
            if (header_end != -1)
                return header_end;
        }
    }
    return -1;
}

void print_radio_stream(radio_stream *stream, ostream& out)
{
    out.write(stream->buffer, stream->length);
    stream->length = 0;
}


string get_title(string& metadata)
{
    boost::regex base_regex("StreamTitle='(.*)';StreamUrl");
    boost::smatch base_match;
    if (boost::regex_search(metadata, base_match, base_regex))
    {
        if (base_match.size() == 2)
        {
                boost::ssub_match base_sub_match = base_match[1];
                string title_str = base_sub_match.str();
                return title_str;
        }
    }
    return string();
}


int main(int argc, char *argv[])
{
    if (argc != 7)
    {
        cerr << "Usage " << argv[0] << " host path r-port file m-port md\n";
        return 1;
    }

    char *host = argv[1];
    char *path = argv[2];
    char *rport = argv[3];
    char *file = argv[4];
    char *mport = argv[5];

    int meta = 0;
    if (string(argv[6]) == "no")
        meta = 0;
    else if (string(argv[6]) == "yes")
        meta = 1;
    else
    {
        cerr << "md should be yes or no\n";
        return 1;
    }

    // laczenie z radyjkiem
    struct addrinfo hints, *res;
    int radio_sockfd;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(host, rport, &hints, &res) != 0)
        syserr("getaddrinfo");

    radio_sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (radio_sockfd == -1)
        syserr("socket");

    if(connect(radio_sockfd, res->ai_addr, res->ai_addrlen) == -1)
        syserr("connect");


    // konfiguracja sluchania na udp
    int control_sockfd;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;

    if (getaddrinfo(NULL, mport, &hints, &res) != 0)
        syserr("getaddrinfo");

    control_sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if(control_sockfd == -1)
        syserr("socket");
    if (bind(control_sockfd, res->ai_addr, res->ai_addrlen) == -1)
        syserr("bind");


    // ustawienie out na wypisanie do pliku lub stdout
    streambuf *buf;
    ofstream of;
    if(string(file) != "-")
    {
        of.open(file);
        buf = of.rdbuf();
    }
    else
        buf = cout.rdbuf();
    ostream out(buf);

    // inicjowanie polaczenia

    string request = create_header(host, rport, path, meta);
    send_request(radio_sockfd, request);
    int end_pos = read_response(radio_sockfd, &header);
    if (end_pos == -1)
    {
        cerr << "invalid header response\n";
        return 1;
    }
    string header_str(header.buffer, end_pos + 4);
    if (!is_header_ok(header_str, end_pos + 4))
    {
        cerr << "header not ok" << endl;
        exit(1);
    }
    read_metaint(header_str);
    if (write_to_radio_stream(&stream, header.buffer + end_pos + 4, header.length - end_pos - 4) == -1)
    {
        cerr << "problem with write_to_radio_stream" << endl;
        exit(1);
    }

    // zainicjowane

    if (stream.length > 0)
        print_radio_stream(&stream, out);



    // polle
    struct pollfd poll_tab[2];
    poll_tab[0].fd = control_sockfd;
    poll_tab[0].events = POLLIN;
    poll_tab[1].fd = radio_sockfd;
    poll_tab[1].events = POLLIN;


    int byte_counter = metaint;
    byte_counter -= header.length - end_pos - 4;
    int read_len = 1;
    int meta_length = 0;
    int quit = 0;
    int pause = 0;
    string stream_title;
    string metadata_str;
    while (quit == 0 && read_len)
    {
        if (poll(poll_tab, 2, -1) == -1) syserr("poll");

        if (poll_tab[0].revents & POLLIN)
        {
            struct sockaddr_in client_address;
            socklen_t rcva_len = (socklen_t) sizeof(client_address);
            int len = recvfrom(control_sockfd, control_buffer, sizeof(control_buffer), 0,
					(struct sockaddr *) &client_address, &rcva_len);
            if (len == -1) syserr("recvfrom");

            string control_msg(control_buffer, len);

            if (control_msg.find("PAUSE") != string::npos)
            {
                if (pause == 1)
                    cerr << "Player already paused" << endl;
                pause = 1;
            }
            else if(control_msg.find("PLAY") != string::npos)
            {
                if (pause == 0)
                    cerr << "Player was running" << endl;
                pause = 0;
            }
            else if (control_msg.find("QUIT") != string::npos)
            {
                quit = 1;
            }
            else if (control_msg.find("TITLE") != string::npos)
            {
                // jezeli serwer nie wysylametadanych to wyslemy pusty string
                socklen_t snda_len = (socklen_t) sizeof(client_address);
                string title = get_title(metadata_str);
                if (title.length() > 0)
                    len = sendto(control_sockfd, title.c_str(), title.length(), 0, (struct sockaddr *) &client_address, snda_len);
                if (len == -1) syserr("sendto");
            }
            else
            {
                cerr << "wrong control command" << endl;
            }
        }

        if (poll_tab[1].revents & POLLIN)
        {
            // czytanie bajtu z dlugoscia
            if (metaint != 0 && byte_counter == 0)
            {
                unsigned char meta_char;
                read_len = read(radio_sockfd, &meta_char, 1);
                if (read_len == -1) syserr("read4");
                meta_length = (int)meta_char;
                meta_length *= 16;
                byte_counter = metaint;
            }
            // czytanie metadanych
            else if (meta_length)
            {
                read_len = read_metadata_stream(radio_sockfd, &metadata, meta_length);
                meta_length -= read_len;
                if (meta_length == 0)
                {
                    metadata_str = string(metadata.buffer, metadata.length);
                    metadata.length = 0;
                }
            }
            else
            {
                if (metaint != 0)
                {
                    read_len = read_radio_stream(radio_sockfd, &stream, byte_counter);
                    byte_counter -= read_len;
                }
                else
                    read_len = read_radio_stream(radio_sockfd, &stream, MAX_RADIO_BUFFER_SIZE - 10);

                if (pause == 0)
                    print_radio_stream(&stream, out);
                else
                    stream.length = 0;
            }
        }

    }

    freeaddrinfo(res);
    return 0;
}
