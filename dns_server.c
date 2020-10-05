/*  클라이언트로부터 domain name 혹은 IP주소를 받아 반대로 변환해주는 DNS 서버 어플리케이션

*** 기능 ***
- 클라이언트로부터 domain name 혹은 IP주소를 받아 반대로 변환해주는 기능 (기본 기능)
- 클라이언트 연결 시, 1회 연결당 최대 30회의 query 가능
- domain name과 IP주소를 구분하여 자동으로 반대로 변환할 수 있음.
- 잘못된 입력의 경우에는 예외 처리(잘못된 domain name, IP주소)
: ip의 경우 0~255의 범위의 숫자만, domain의 경우 알파벳과 숫자만 허용
- 해당 domain 혹은 자동으로 외부 DNS를 통해 업데이트

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

#define BUFSIZE 50 // 한번에 통신 가능한 최대 BUFSIZE
#define MAXSIZE 30 // 클라이언트가 1회 접속당 query 가능한 최대 dns 혹은 ip 갯수

// 클라이언트 로그파일을 위한 기록용 구조체 사용자 정의
typedef struct _LOGDATA
{
    char clnt_addr[BUFSIZE];
    struct tm *start;
    struct tm *end;
    char dns_or_ip[MAXSIZE][BUFSIZE];
    int di_cnt;
    // 클라이언트 IP, 시작시간, 종료시간, 클라이언트가 물어본 dns/ip, 클라이언트가 물어본 dns/ip갯수
} LOGDATA;

// 서버 DNS table 관리를 위한 기록용 구조체 사용자 정의 (공유 메모리로 수정 예정)
typedef struct _DNSTABLE
{
    char dns[BUFSIZE];
    char *ip_addr[BUFSIZE];
    int hit_ratio;
} DNSTABLE;

void error_handling(char *message);
void get_now_time(struct tm *nt);
void write_log(LOGDATA lgdata, FILE *log_file);
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
    char message[BUFSIZE] = "";
    pid_t pid;

    // domain과 IP주소를 구분하는 작업을 위해 필요한 변수들
    int iter = 0;               // 데이터를 구분하기 위한 while문에서 사용할 변수
    int flag = 0;               // 0: IP주소, 1: domain  2: 올바르지 않은 ip 3:특수문자가 들어간 domain
    char ip_or_domain[BUFSIZE]; // strtok으로 잘라기 전의 변수를 저장하기 위한 변수
    char *clnt_data[5];         // strtok으로 자른 데이터를 보관할 변수

    // domain과 IP주소간의 변환을 위한 변수들
    struct hostent *myhost;
    struct in_addr myinaddr;
    struct sockaddr_in addr;

    // log 작성을 위한 변수들
    FILE *log_file, *dns_file; // log 파일 및 dns 테이블 파일
    char *ip_addr;             // client IP를 기록하기 위한 변수
    struct tm *log_time = (struct tm *)malloc(sizeof(struct tm)); // log파일을 위한 tm 구조체
    char dir_name[MAXSIZE]; // 월별 디렉토리 를 만들기 위한 문자열 변수
    char lg_file_name[MAXSIZE]; // 일별 로그파일을 만들기 위한 문자열 변수

    get_now_time(log_time);
    sprintf(dir_name, "%d년 %d월", log_time->tm_year, log_time->tm_mon);

    mkdir(dir_name, 0777);

    sprintf(lg_file_name, "%s/%d월 %d일_log.log", dir_name, log_time->tm_mon, log_time->tm_mday);

    log_file = fopen(lg_file_name, "a");
    dns_file = fopen("dns_table.txt", "a");

    if (log_file == NULL)
        printf("로그파일 열기 실패\n");

    if (dns_file == NULL)
        printf("dns 테이블 로딩 실패!\n");
    
    free(log_time);

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
            close(serv_sock);
            LOGDATA lgdata;

            // printf("첫번째 구간 진입 \n");
            // 구조체 초기화
            lgdata.di_cnt = 0;
            for (int i = 0; i < MAXSIZE; i++)
                strcpy(lgdata.dns_or_ip[i], "\0");
            lgdata.start = (struct tm *)malloc(sizeof(struct tm));
            lgdata.end = (struct tm *)malloc(sizeof(struct tm));

            ip_addr = inet_ntoa(clnt_addr.sin_addr);
            // fprintf(log_file, "client ip addr : %s\n", ip_addr);

            /* 클라이언트 ip주소 및 연결 시간 저장*/
            strcpy(lgdata.clnt_addr, ip_addr);
            get_now_time(lgdata.start);

            /* 자식 프로세스의 처리영역 : 데이터 수신 및 전송 */
            printf("두번째 구간 진입 \n");

            while ((str_len = read(clnt_sock, message, BUFSIZE)) != 0)
            {
                printf("클라이언트가 보낸 문자열 : %s\n", message);
                // printf("세번째 구간 진입 \n");

                strcpy(ip_or_domain, message);                    // 받은 message를 ip_or_domain 변수에 저장
                strcpy(lgdata.dns_or_ip[lgdata.di_cnt], message); // 로그를 기록할 변수에 물어본 query 저장
                lgdata.di_cnt++;

                // printf("네번째 구간 진입 \n");

                // strtok 함수를 이용하여 문자열을 잘라서 저장(dns와 ip 판별을 위함)
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
                // ip주소일 시 실행되는 코드
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
                else if (flag == 1) // domain 입력시 실행되는 코드
                {
                    printf("domain이 입력되었습니다.\n");
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
                            myinaddr.s_addr = *((__u_long *)(myhost->h_addr_list[i]));
                            printf("address for host:\t\t%s\n", inet_ntoa(myinaddr));
                            i++;
                        }
                    }
                }
                else if (flag == 2) // 잘못된 ip 주소 입력
                {
                    printf("올바른 ip주소가 아닙니다. 올바른 ip주소를 입력해주세요.\n");
                }
                else if (flag == 3) // 잘못된 domain 주소 입력
                {
                    printf("올바른 domain이 아닙니다. 올바른 domain을 입력해주세요.\n");
                }

                // printf("7번째 구간 진입 \n");

                if (lgdata.di_cnt >= MAXSIZE) // 최대 갯수의 질문을 할시 강제종료
                {
                    //write로 변경할 것
                    printf("1회 연결시 최대 질문 가능한 쿼리 개수인 30개를 입력하셨습니다.\n");
                    printf("연결을 종료합니다. 더 질문하려면 재연결해 주세요.\n");
                    break;
                }
            }
            close(clnt_sock);
            get_now_time(lgdata.end);

            write_log(lgdata, log_file);

            // fprintf(log_file, "********** 클라이언트 접속 정보 **********\n");
            // fprintf(log_file, "클라이언트 IP 주소 : %s\n", lgdata.clnt_addr);
            // fprintf(log_file, "접속 시간 : %d시 %d분 %d초\n", lgdata.start->tm_hour, lgdata.start->tm_min, lgdata.start->tm_sec);
            // fprintf(log_file, "------물어본 dns와 쿼리 목록------\n");
            // for (int i = 0; i < lgdata.di_cnt; i++)
            //     fprintf(log_file, "%s\n", lgdata.dns_or_ip[i]);
            // fprintf(log_file, "접속 종료 시간 : %d시 %d분 %d초\n", lgdata.end->tm_hour, lgdata.end->tm_min, lgdata.end->tm_sec);

            free(lgdata.start);
            free(lgdata.end);

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
 * @param nt : 시간을 기록하기위해 받아올 struct tm 구조체의 주소
 */
void get_now_time(struct tm *nt)
{
    time_t now_time;
    struct tm *t;

    time(&now_time);
    t = (struct tm *)localtime(&now_time);

    nt->tm_year = t->tm_year + 1900;
    nt->tm_mon = t->tm_mon + 1;
    nt->tm_mday = t->tm_mday;
    nt->tm_hour = t->tm_hour;
    nt->tm_min = t->tm_min;
    nt->tm_sec = t->tm_sec;
}

/** 클라이언트 종료시 logFile을 저장
 * 
 */
void write_log(LOGDATA lgdata, FILE *log_file)
{
    fprintf(log_file, "********** 클라이언트 접속 정보 **********\n");
    fprintf(log_file, "클라이언트 IP 주소 : %s\n", lgdata.clnt_addr);
    fprintf(log_file, "접속 시간 : %d시 %d분 %d초\n", lgdata.start->tm_hour, lgdata.start->tm_min, lgdata.start->tm_sec);
    fprintf(log_file, "------물어본 dns와 쿼리 목록------\n");
    for (int i = 0; i < lgdata.di_cnt; i++)
        fprintf(log_file, "%s\n", lgdata.dns_or_ip[i]);
    fprintf(log_file, "접속 종료 시간 : %d시 %d분 %d초\n", lgdata.end->tm_hour, lgdata.end->tm_min, lgdata.end->tm_sec);
}

// dns를 받아오는 함수 2개 생성 예정(domain, IP)