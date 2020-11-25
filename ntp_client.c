/*
 *
 * (C) 2014 David Lettier.
 *
 * http://www.lettier.com/
 *
 * NTP client.
 *
 * Compiled with gcc version 4.7.2 20121109 (Red Hat 4.7.2-8) (GCC).
 *
 * Tested on Linux 3.8.11-200.fc18.x86_64 #1 SMP Wed May 1 19:44:27 UTC 2013 x86_64 x86_64 x86_64 GNU/Linux.
 *
 * To compile: $ gcc main.c -o ntpClient.out
 *
 * Usage: $ ./ntpClient.out
 *
 */

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

#define NTP_TIMESTAMP_DELTA 2208988800ull

#define LI(packet)   (uint8_t) ((packet.li_vn_mode & 0xC0) >> 6) // (li   & 11 000 000) >> 6
#define VN(packet)   (uint8_t) ((packet.li_vn_mode & 0x38) >> 3) // (vn   & 00 111 000) >> 3
#define MODE(packet) (uint8_t) ((packet.li_vn_mode & 0x07) >> 0) // (mode & 00 000 111) >> 0

void error( char* msg )
{
    perror( msg ); // Print the error message to stderr.

    exit( 0 ); // Quit the process.
}

int main( int argc, char* argv[ ] )
{
	int sockfd, n; //套接字文件描述符，并且从套接字写入/读取的n返回结果。
	int portno = 123; // NTP UDP端口号。
	char* host_name = "us.pool.ntp.org"; // NTP服务器主机名。

	//定义48字节NTP数据包协议的结构。
	typedef struct {
		uint8_t li_vn_mode;		// Eight bits. li, vn, and mode.
								// li.   2bit. Leap indicator.
								// vn.   3bit. 协议的版本号。
								// mode. 3bit. 客户端将为客户端选择模式3。
		uint8_t stratum;		// 8bits 本地时钟的层级。
		uint8_t poll;			// 8bits 连续消息之间的最大间隔。
		uint8_t precision;		// 8bits 本地时钟的精度。

		uint32_t rootDelay;      // 32 bits. 总往返延迟时间。
		uint32_t rootDispersion; // 32 bits. 来自主时钟源的最大错误声。
		uint32_t refId;          // 32 bits. 参考时钟标识符。

		uint32_t refTm_s;        // 32 bits. 参考时间戳秒。
		uint32_t refTm_f;        // 32 bits. 参考时间戳的秒数。

		uint32_t origTm_s;       // 32 bits. 原始时间戳记秒。
		uint32_t origTm_f;       // 32 bits. 始发时间戳记秒。

		uint32_t rxTm_s;         // 32 bits. 收到时间戳记秒。
		uint32_t rxTm_f;         // 32 bits. 收到的时间戳记秒。

		uint32_t txTm_s;         // 32 bits. 客户最关心的领域。 传输时间戳秒。
		uint32_t txTm_f;         // 32 bits. 传输时间戳的秒数。

	} ntp_packet;			//总计：384位或48个字节。

	//创建数据包并将其清零。 全部48个字节值得。
	ntp_packet packet = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
	memset( &packet, 0, sizeof( ntp_packet ) );

	//如果li = 0，vn = 3，mode = 3，则将第一个字节的位设置为00,011,011。其余部分将保持为零。
	*( ( char * ) &packet + 0 ) = 0x1b; //以10为基数代表27或以2为基数00011011

	//创建一个UDP套接字，将主机名转换为IP地址，设置端口号，
	//连接到服务器，发送数据包，然后读取返回数据包。
	struct sockaddr_in serv_addr;	// 服务器地址数据结构。
	struct hostent *server;			// 服务器数据结构。

	sockfd = socket( AF_INET, SOCK_DGRAM, IPPROTO_UDP ); // 创建 UDP socket.
	if ( sockfd < 0 ) {
		error( "ERROR opening socket" );
	}
	
	server = gethostbyname( host_name ); // Convert URL to IP.
	if ( server == NULL ) {
		error( "ERROR, no such host" );
	}
	
	//将服务器地址结构清零。
	bzero( ( char* ) &serv_addr, sizeof( serv_addr ) );
	
	serv_addr.sin_family = AF_INET;
	// Copy the server's IP address to the server address structure.
	bcopy( ( char* )server->h_addr, ( char* ) &serv_addr.sin_addr.s_addr, server->h_length );
	// Convert the port number integer to network big-endian style and save it to the server address structure.
	serv_addr.sin_port = htons( portno );
	// Call up the server using its IP address and port number.
	if ( connect( sockfd, ( struct sockaddr * ) &serv_addr, sizeof( serv_addr) ) < 0 ) {
		error( "ERROR connecting" );
	}
	
	// Send it the NTP packet it wants. If n == -1, it failed.
	n = write( sockfd, ( char* ) &packet, sizeof( ntp_packet ) );
	if ( n < 0 ) {
		error( "ERROR writing to socket" );
	}
	
	// Wait and receive the packet back from the server. If n == -1, it failed.
	n = read( sockfd, ( char* ) &packet, sizeof( ntp_packet ) );
	if ( n < 0 ) {
		error( "ERROR reading from socket" );
	}

	//这两个字段包含数据包离开NTP服务器时的时间戳秒。
	//秒数对应于自1900年以来经过的秒数。
	// ntohl（）将位/字节顺序从网络转换为主机的“字节序”。
	packet.txTm_s = ntohl( packet.txTm_s ); // Time-stamp seconds.
	packet.txTm_f = ntohl( packet.txTm_f ); // Time-stamp fraction of a second.

	//从数据包离开服务器时提取代表时间戳秒的32位（自NTP时代开始）。
	//从1900年的秒数中减去70年的秒数。
	//这距离1970年的UNIX时代只有几秒钟的时间。
	// (1900)------------------(1970)**************************************(Time Packet Left the Server)
	time_t txTm = ( time_t ) ( packet.txTm_s - NTP_TIMESTAMP_DELTA );
	// Print the time we got from the server, accounting for local timezone and conversion from UTC time.
	printf( "Time: %s", ctime( ( const time_t* ) &txTm ) );
	
	struct tm *p;
	time(&txTm); /*获得time_t结构的时间，UTC时间*/
	//p = gmtime(&txTm); /*转换为struct tm结构的UTC时间*/
	p = localtime(&txTm); /*转换为struct tm结构的当地时间*/

	
	int sec		= p->tm_sec;  /*秒，正常范围0-59， 但允许至61*/
	int min		= p->tm_min;  /*分钟，0-59*/
	int hour	= p->tm_hour; /*小时， 0-23*/
	int day		= p->tm_mday; /*日，即一个月中的第几天，1-31*/
	int mon		= 1+p->tm_mon;  /*月， 从一月算起，0-11  1+p->tm_mon;*/
	int year	= 1900+p->tm_year;  /*年， 从1900至今已经多少年  1900＋ p->tm_year;*/

	printf("%d-%d-%d %d:%d:%d\r\n", year, mon, day, hour, min, sec);

	return 0;
}
