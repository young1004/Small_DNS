/*  클라이언트로부터 domain name 혹은 IP주소를 받아 반대로 변환해주는 DNS 서버 어플리케이션

*** 기능 ***
- 클라이언트로부터 domain name 혹은 IP주소를 받아 반대로 변환해주는 기능 (기본 기능)
- domain name과 IP주소를 구분하여 자동으로 반대로 변환할 수 있음.
- 잘못된 입력의 경우에는 예외 처리(잘못된 domain name, IP주소)
- 해당 domain 혹은 자동으로 외부 DNS를 통해 업데이트

*** 특수 기능(추가 예정) ***
- 일정 주기마다 자동으로 외부 DNS서버를 통해 DNS 테이블 업데이트

* 작성 일시 : 2020-10-06
* 작성자    : 조용구
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <ctype.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>

void error_handling(char *message);
void write_time(FILE *log_file);

int main(int argc, char **argv)
{
    // 서버와 클라이언트 연결 및 통신을 위한 변수들
    int serv_sock;
    int clnt_sock;
    struct sockaddr_in serv_addr;
    struct sockaddr_in clnt_addr;

    int clnt_addr_size;
    char message[50] = "";
    char *ip_addr;
    char *clnt_data[5];

    // domain과 IP주소를 구분하는 작업을 위해 필요한 변수들
    int iter = 0;
    int flag = 0;          // 0: IP주소, 1: domain  2: 올바르지 않은 ip 3:특수문자가 들어간 domain
    char ip_or_domain[50]; // strtok으로 잘라지는 변수를 저장하기 위한 변수

    // domain과 IP주소간의 변환을 위한 변수들
    struct hostent *myhost;
    struct in_addr myinaddr;
    struct sockaddr_in addr;

    FILE *log_file;

    log_file = fopen("conn_log.log", "a");

    if (log_file == NULL)
        printf("파일열기 실패\n");

    if (argc != 2)
    {
        printf("Usage: %s <port> \n", argv[0]);
        exit(1);
    }

    serv_sock = socket(PF_INET, SOCK_STREAM, 0);

    if (serv_sock == -1)
        error_handling("socket() error");

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(atoi(argv[1]));

    if (bind(serv_sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) == -1)
        error_handling("bind() error");

    if (listen(serv_sock, 5) == -1)
        error_handling("listen() error");

    clnt_addr_size = sizeof(clnt_addr);
    clnt_sock = accept(serv_sock, (struct sockaddr *)&clnt_addr, &clnt_addr_size);

    if (clnt_sock == -1)
        error_handling("accept() error");

    ip_addr = inet_ntoa(clnt_addr.sin_addr);
    fprintf(log_file, "client ip addr : %s\n", ip_addr);
    write_time(log_file);

    read(clnt_sock, message, sizeof(message) - 1);
    strcpy(ip_or_domain, message);

    // 들어온 domain 혹은 IP주소를 분리하여 저장
    while (1)
    {
        if (iter == 0)
            clnt_data[iter] = strtok(message, ".");
        else
            clnt_data[iter] = strtok(NULL, ".");

        if (iter > sizeof(clnt_data) / sizeof(char *) - 1 || clnt_data[iter] == NULL)
            break;

        iter++;
    }
    iter = 0;

    // printf("크기: %d\n", sizeof(clnt_data)/sizeof(char*));
    // 자른 문자열을 이용하여 ip주소인지 domain인지 판별
    while (1)
    {
        if (iter > sizeof(clnt_data) / sizeof(char *) - 1 || clnt_data[iter] == NULL)
            break;

        // 문자가 하나라도 존재하면 domain
        for (int i = 0; i < strlen(clnt_data[iter]); i++)
        {
            if (isdigit(clnt_data[iter][i]) == 0)
                flag = 1;

            if (flag == 1)
            {
                if (isalpha(clnt_data[iter][i]) == 0 && isdigit(clnt_data[iter][i]) == 0)
                {
                    flag = 3;
                    break;
                }
            }
        }
        // 숫자이면 올바른 범위의 ip 주소인지 확인
        if (flag == 0)
            if (atoi(clnt_data[iter]) < 0 || atoi(clnt_data[iter]) > 255)
                flag = 2;
        
        if(flag == 2 || flag == 3)
            break;
        iter++;
    }
    iter = 0;

    if (flag == 0)
    {
        printf("ip주소가 입력되었습니다.\n");
        memset(&addr, 0, sizeof(addr));
        addr.sin_addr.s_addr = inet_addr(argv[1]);

        myhost = gethostbyaddr((char *)&addr.sin_addr, 4, AF_INET);

        if (!myhost)
            error_handling("gethost... error");

        printf("Officially name : %s \n\n", myhost->h_name);

        puts("Aliases-----------");
        for (int i = 0; myhost->h_aliases[i]; i++)
        {
            puts(myhost->h_aliases[i]);
        }
        printf("Address Type : %s \n", myhost->h_addrtype == AF_INET ? "AF_INET" : "AF_INET6");
        puts("IP Address--------");
        for (int i = 0; myhost->h_addr_list[i]; i++)
        {
            puts(inet_ntoa(*(struct in_addr *)myhost->h_addr_list[i]));
        }
    }
    else if (flag == 1)
    {
        printf("domain 주소가 입력되었습니다.\n");
        myhost = gethostbyname(ip_or_domain);

        if (myhost == 0)
        {
            printf("erro occurs .. at 'gethostbyname'.\n\n\n");
            exit(1);
        }

        // 호스트 이름 출력
        printf("official host name : \t\t %s\n", myhost->h_name);
        int i = 0;
        //호스트 별명 출력
        while (myhost->h_aliases[i] != NULL)
        {
            printf("aliases name : \t\t%s\n", myhost->h_aliases[i]);
            i++;
        }

        //호스트 주소체계 출력
        printf("host address type : \t\t%d\n", myhost->h_addrtype);
        //호스트 주소 길이 출력
        printf("length of host address : \t%d\n", myhost->h_length);
        //호스트 주소를 dotted decimal 형태로 출력
        i = 0;
        while (myhost->h_addr_list[i] != NULL)
        {
            myinaddr.s_addr = *((u_long *)(myhost->h_addr_list[i]));
            printf("address for host:\t\t%s\n", inet_ntoa(myinaddr));
            i++;
        }
    }
    else if (flag == 2)
    {
        printf("올바른 ip주소가 아닙니다. 올바른 ip주소를 입력해주세요.\n");
    }
    else if (flag == 3)
    {
        printf("올바른 domain이 아닙니다. 올바른 domain을 입력해주세요.\n");
    }

    write(clnt_sock, message, sizeof(message));
    close(clnt_sock);

    write_time(log_file);
    fclose(log_file);

    return 0;
}
/** error 메시지를 보여주는 함수
 * @param   a : 보여줄 메시지 문자열의 시작 주소
 */
void error_handling(char *message)
{
    fputs(message, stderr);
    fputc('\n', stderr);
    exit(1);
}

/** 연결 시간에 대한 정보를 기록하는 함수
 */
void write_time(FILE *log_file)
{
    time_t now_time;
    struct tm *t;

    time(&now_time);
    t = (struct tm *)localtime(&now_time);

    fprintf(log_file, "연결 시간: %d년 %d월 %d일 %d시 %d분 %d초\n", t->tm_year + 1900, t->tm_mon + 1, t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec);
}
