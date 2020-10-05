/*  클라이언트로부터 domain name 혹은 IP주소를 받아 반대로 변환해주는 DNS 서버 어플리케이션

*** 기능 ***
- 클라이언트로부터 domain name 혹은 IP주소를 받아 반대로 변환해주는 기능 (기본 기능)
- domain name과 IP주소를 구분하여 자동으로 반대로 변환할 수 있음.
- 잘못된 입력의 경우에는 예외 처리(잘못된 domain name, IP주소)
: ip의 경우 0~255의 범위의 숫자만, domain의 경우 알파벳과 숫자만 허용
- 해당 domain 혹은 자동으로 외부 DNS를 통해 업데이트

*** 특수 기능(추가 예정) ***
- 일정 주기마다 자동으로 외부 DNS서버를 통해 DNS 테이블 업데이트

* 작성 일시 : 2020-10-06
* 작성자    : 조용구
*/

#define _XOPEN_SOURCE 200

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <ctype.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <signal.h>

#define BUFSIZE 50

typedef struct _LOGDATA
{
    char clnt_addr[BUFSIZE];
    struct tm *start;
    struct tm *end;

    

    // 클라이언트 IP, 시작시간, 종료시간, 클라이언트가 물어본 dns혹은 ip주소들
} LOGDATA;


void error_handling(char *message);
struct tm *get_now_time();
void z_handler(int sig);

int main(int argc, char **argv)
{
    // 서버와 클라이언트 연결 및 통신을 위한 변수들
    int serv_sock;
    int clnt_sock;
    struct sockaddr_in serv_addr;
    struct sockaddr_in clnt_addr;
    struct sigaction act;

    int addr_size, str_len, state;
    char message[50] = "";
    pid_t pid;

    // domain과 IP주소를 구분하는 작업을 위해 필요한 변수들
    int iter = 0;          // 데이터를 구분하기 위한 while문에서 사용할 변수
    int flag = 0;          // 0: IP주소, 1: domain  2: 올바르지 않은 ip 3:특수문자가 들어간 domain
    char ip_or_domain[50]; // strtok으로 잘라기 전의 변수를 저장하기 위한 변수
    char *clnt_data[5];    // strtok으로 자른 데이터를 보관할 변수

    // domain과 IP주소간의 변환을 위한 변수들
    struct hostent *myhost;
    struct in_addr myinaddr;
    struct sockaddr_in addr;

    // log 작성을 위한 변수들
    FILE *log_file, *dns_file; // log 파일 및 dns 테이블 파일
    char *ip_addr;             // client IP를 기록하기 위한 변수

    log_file = fopen("conn_log.log", "a");
    dns_file = fopen("dns_table.txt", "a");

    if (log_file == NULL)
        printf("로그파일 열기 실패\n");

    if (dns_file == NULL)
        printf("dns 테이블 로딩 실패!\n");

    if (argc != 2)
    {
        printf("Usage : %s <port>\n", argv[0]);
        exit(1);
    }

    act.sa_handler = z_handler;
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;
    state = sigaction(SIGCHLD, &act, 0); /* 시그널 핸들러 등록 */
    if (state != 0)
    {
        puts("sigaction() error");
        exit(1);
    }

    serv_sock = socket(PF_INET, SOCK_STREAM, 0);
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(atoi(argv[1]));

    if (bind(serv_sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) == -1)
        error_handling("bind() error");

    if (listen(serv_sock, 5) == -1)
        error_handling("listen() error");
    while (1)
    {
        addr_size = sizeof(clnt_addr);
        clnt_sock = accept(serv_sock, (struct sockaddr *)&clnt_addr, &addr_size);
        if (clnt_sock == -1)
            continue;

        /* 클라이언트와의 연결을 독립적으로 생성 */
        if ((pid = fork()) == -1)
        { /* fork 실패 시 */
            close(clnt_sock);
            continue;
        }
        else if (pid > 0)
        { /* 부모 프로세스인 경우 */
            puts("연결 생성");
            close(clnt_sock);
            continue;
        }
        else
        { /* 자식 프로세스의 경우 */
            LOGDATA lgdata;

            ip_addr = inet_ntoa(clnt_addr.sin_addr);
            // fprintf(log_file, "client ip addr : %s\n", ip_addr);
            strcpy(lgdata.clnt_addr, ip_addr);
            lgdata.start = get_now_time();

            close(serv_sock);
            /* 자식 프로세스의 처리영역 : 데이터 수신 및 전송 */

            // read(clnt_sock, message, sizeof(message) - 1);
            while ((str_len = read(clnt_sock, message, BUFSIZE)) != 0)
            {
                strcpy(ip_or_domain, message); // 받은 message를 ip_or_domain 변수에 저장


                // strtok 으로 문자열을 잘라서 저장
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

                        // 문자열이면 특수문자가 포함되어 있는지 검사
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

                    if (flag == 2 || flag == 3)
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

                    if (!myhost) // dns에 해당하는 값을 못 찾을시
                    {
                        error_handling("gethost... error");
                    }
                    else
                    {
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
                }
                else if (flag == 1)
                {
                    printf("domain 주소가 입력되었습니다.\n");
                    myhost = gethostbyname(ip_or_domain);

                    if (myhost == 0)
                    {
                        printf("erro occurs .. at 'gethostbyname'.\n\n\n");
                    }
                    else
                    {
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
                }
                else if (flag == 2)
                {
                    printf("올바른 ip주소가 아닙니다. 올바른 ip주소를 입력해주세요.\n");
                }
                else if (flag == 3)
                {
                    printf("올바른 domain이 아닙니다. 올바른 domain을 입력해주세요.\n");
                }
            }
            puts("연결 종료");
            close(clnt_sock);
            get_now_time(log_file);

            exit(0);
        }
    }
    fclose(log_file);
    fclose(dns_file);
    return 0;
}

void z_handler(int sig)
{
    pid_t pid;
    int rtn;

    pid = waitpid(-1, &rtn, WNOHANG);
    printf("소멸된 좀비의 프로세스 ID : %d \n", pid);
    printf("리턴된 데이터 : %d \n\n", WEXITSTATUS(rtn));
}

/** error 메시지를 보여주는 함수
 * @param   a : 보여줄 메시지 문자열의 시작 주소
 */
void error_handling(char *message)
{
    fputs(message, stderr);
    fputc('\n', stderr);
    // exit(1);
}

/** 연결 시간에 대한 정보를 리턴하는 함수
 * @return {struct tm*} : 연결 시간에 대한 정보를 담고있는 구조체 포인터 반환
 */
struct tm *get_now_time()
{
    time_t now_time;
    struct tm *t = malloc(sizeof(struct tm));

    time(&now_time);
    t = (struct tm *)localtime(&now_time);

    return t;
    // fprintf(log_file, "연결 시간: %d년 %d월 %d일 %d시 %d분 %d초\n", t->tm_year + 1900, t->tm_mon + 1, t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec);
}

/** 클라이언트 종료시 logFile을 저장
 * 
 */
void write_log(LOGDATA *log)
{

}

// dns를 받아오는 함수 2개 생성 예정(domain, IP)