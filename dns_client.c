
/* 서버에 domain name을 보내면 해당 domain의 IP주소를, IP주소를 보내면 domain name을 반환받는 클라이언트 프로그램
* 작성 일시 : 2020-10-06
* 작성자    : 조용구
* 입력 방식 : 파일 실행시 IP혹은 domain을 첫번째 인수로 입력, 두번째 인수로 포트번호 입력
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>

#define BUFSIZE 50

void error_handling(char *message);

int main(int argc, char **argv)
{
    int sock;
    pid_t pid;
    char message[BUFSIZE];
    int str_len;

    struct sockaddr_in serv_addr;
    if (argc != 3)
    {
        printf("Usage : %s <IP> <port>\n", argv[0]);
        exit(1);
    }

    sock = socket(PF_INET, SOCK_STREAM, 0);
    if (sock == -1)
        error_handling("socket() error");

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = inet_addr(argv[1]);
    serv_addr.sin_port = htons(atoi(argv[2]));

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) == -1)
        error_handling("connect() error!");

    pid = fork();
    if (pid == 0)
    {
        while (1)
        {
            fputs("insert IP or domain (q to quit) : ", stdout);
            fgets(message, BUFSIZE, stdin);
            if (!strcmp(message, "q\n"))
            {
                shutdown(sock, SHUT_WR);
                close(sock);
                exit(0);
            }
            write(sock, message, strlen(message));
        } /* while(1) end */
    }     /* if(pid==0) end */
    else
    {
        while (1)
        {
            int str_len = read(sock, message, BUFSIZE);
            if (str_len == 0)
            {
                exit(0);
            }

            message[str_len] = 0;
            printf("서버로부터 전송된 메시지 : %s\n", message);
        } /* while(1) end */
    }     /* else end*/

    close(sock);
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