
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

void error_handling(char *message);

int main(int argc, char **argv)
{
    int sock;
    int clnt_sock;
    struct sockaddr_in serv_addr;
    
    char message[30];
    int str_len;

    if (argc != 3)
    {
        printf("Usage: %s <IP/Domain> <port> \n", argv[0]);
        exit(1);
    }
    sock = socket(PF_INET, SOCK_STREAM, 0);
    if (sock == -1)
        error_handling("socket() error");

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY); // 다른 서버 연결시 인수 추가
    serv_addr.sin_port = htons(atoi(argv[2]));

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) == -1)
        error_handling("connet() error");

    strcpy(message, argv[1]);
    write(sock, message, sizeof(message));

    str_len = read(sock, message, sizeof(message) - 1);

    if (str_len == -1)
        error_handling("read() error!");

    printf("Message from server : '%s' ", message);

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

