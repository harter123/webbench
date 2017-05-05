/*
 * (C) Radim Kolar 1997-2004
 * This is free software, see GNU Public License version 2 for
 * details.
 *
 * Simple forking WWW Server benchmark:
 *
 * Usage:
 *   webbench --help
 *
 * Return codes:
 *    0 - sucess
 *    1 - benchmark failed (server is not on-line)
 *    2 - bad param
 *    3 - internal error, fork failed
 * 
 */ 
#include "socket.c"
#include <unistd.h>
#include <sys/param.h>
#include <rpc/types.h>
#include <getopt.h>
#include <strings.h>
#include <time.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <string.h>
#include <time.h>

/* values */
volatile int timerexpired=0;
int speed=0;
int failed=0;
int bytes=0;

double min_time= 120000000000.0;
double max_time=0.0;
double all_time = 0.0;
/* globals */
int http10=1; /* 0 - http/0.9, 1 - http/1.0, 2 - http/1.1 */
/* Allow: GET, HEAD, OPTIONS, TRACE */
#define METHOD_GET 0
#define METHOD_HEAD 1
#define METHOD_OPTIONS 2
#define METHOD_TRACE 3
#define METHOD_POST 4
#define PROGRAM_VERSION "1.5"
int method=METHOD_GET;
int clients=1;
int force=0;
int force_reload=0;
int proxyport=80;
char *proxyhost=NULL;
int benchtime=30;
/* internal */
int mypipe[2];
char host[MAXHOSTNAMELEN];
#define REQUEST_SIZE 2048
char request[REQUEST_SIZE];

#define POSTDATA_SIZE 512
char postdata[POSTDATA_SIZE] = {0};
char postdatalen[5] = {0};

/* multiple postdata */
char *postdataall = NULL;
int postdataallline = 0;
char *requestall = NULL;
int requestallsize = 0;

// head data in file
#define HEADDATA_SIZE 128
char *headdataall = NULL;
int headdataallline = 0;

// head data in command
char headstr[HEADDATA_SIZE] = {0};
int headlen = 0;

//assert data in command
char assertstr[HEADDATA_SIZE] = {0};
char *assertlist[10];
int assertlen = 0;

// char *content_type = "application/x-www-form-urlencoded";

static const struct option long_options[]=
{
	{"force",no_argument,&force,1},
	{"reload",no_argument,&force_reload,1},
	{"time",required_argument,NULL,'t'},
	{"help",no_argument,NULL,'?'},
	{"http09",no_argument,NULL,'9'},
	{"http10",no_argument,NULL,'1'},
	{"http11",no_argument,NULL,'2'},
	{"get",no_argument,&method,METHOD_GET},
	{"head",no_argument,&method,METHOD_HEAD},
	{"options",no_argument,&method,METHOD_OPTIONS},
	{"trace",no_argument,&method,METHOD_TRACE},
	{"post",required_argument,NULL,'P'},
	{"file",required_argument,NULL,'F'},
	{"version",no_argument,NULL,'V'},
	{"proxy",required_argument,NULL,'p'},
	{"clients",required_argument,NULL,'c'},
	{"head",required_argument,NULL,'h'},
	{"headfile",required_argument,NULL,'H'},
	{"assert",required_argument,NULL,'A'},
	{NULL,0,NULL,0}
};

/* prototypes */
static void benchcore(const char* host,const int port, const char *request);
static int bench(void);
static void build_request(const char *url);

static void mark_time(clock_t start,clock_t finish);//子进程记录时间数据
static void set_all_time(double min,double max,double all);//父进程统计时间

static int assertrsp(char *rsp);//用于断言

static void alarm_handler(int signal)
{
	timerexpired=1;
}	

static void usage(void)
{
	fprintf(stderr,
		"webbench [option]... URL\n"
		"  -f|--force               Don't wait for reply from server.\n"
		"  -r|--reload              Send reload request - Pragma: no-cache.\n"
		"  -t|--time <sec>          Run benchmark for <sec> seconds. Default 30.\n"
		"  -p|--proxy <server:port> Use proxy server for request.\n"
		"  -c|--clients <n>         Run <n> HTTP clients at once. Default one.\n"
		"  -9|--http09              Use HTTP/0.9 style requests.\n"
		"  -1|--http10              Use HTTP/1.0 protocol.\n"
		"  -2|--http11              Use HTTP/1.1 protocol.\n"
		"  --get                    Use GET request method.\n"
		"  --head                   Use HEAD request method.\n"
		"  --options                Use OPTIONS request method.\n"
		"  --trace                  Use TRACE request method.\n"
		"  -P|--post                Use POST request method.\n"
		"  -h|--head                Add request head. example: -h 'Accept:*;content_type:json'. \n"
		"  -H|--headfile            Add request head from file. the num of char in a line must be less then 128.\n"	
		"  -F|--file                Use POST request method from file. The num of char in a line must be less then 1024.\n"  
		"  -?|--help                This information.\n"
		"  -A|--assert              The key for assert.More than one keys use ';',must be less then 128.\n"
		"  -V|--version             Display program version.\n"
		);
};
int main(int argc, char *argv[])
{
	int opt=0;
	int options_index=0;
	char *tmp=NULL;

	if(argc==1)
	{
		usage();
		return 2;
	} 

	while((opt=getopt_long(argc,argv,"912Vfrt:p:P:F:c:h:H:A:T:?",long_options,&options_index))!=EOF )
	{
		switch(opt)
		{
			case  0 : break;
			case 'f': force=1;break;
			case 'r': force_reload=1;break; 
			case '9': http10=0;break;
			case '1': http10=1;break;
			case '2': http10=2;break;
			case 'V': printf(PROGRAM_VERSION"\n");exit(0);
			case 't': benchtime=atoi(optarg);break;	     
			case 'p':
			{
				/* proxy server parsing server:port */
				tmp=strrchr(optarg,':');
				proxyhost=optarg;
				if(tmp==NULL)
				{
					break;
				}
				if(tmp==optarg)
				{
					fprintf(stderr,"Error in option --proxy %s: Missing hostname.\n",optarg);
					return 2;
				}
				if(tmp==optarg+strlen(optarg)-1)
				{
					fprintf(stderr,"Error in option --proxy %s Port number is missing.\n",optarg);
					return 2;
				}
				*tmp='\0';
				proxyport=atoi(tmp+1);break;
			}
			case 'P':
			{
				snprintf(postdata, sizeof(postdata), "%s", optarg);
				snprintf(postdatalen, sizeof(postdatalen), "%ld", strlen(postdata));
				method = METHOD_POST;
				break;
			}
			case 'F':
			{
				if(get_postdata4file(optarg))
					return 3;
				method = METHOD_POST;
				break;
			}
			case 'H':
			{
				//to do
				if(get_headdata4file(optarg))
				{
					printf("read head file failed!");
					return 3;
				}				
				break;
			}
			case 'h':
			{
				headlen = HEADDATA_SIZE > strlen(optarg) ? strlen(optarg) : HEADDATA_SIZE;
				strcpy(headstr, optarg);
				break;
			}
			case 'A':
			{
				assertlen = strlen(optarg);
				strcpy(assertstr, optarg);
				break;
			}
			case ':':
			case '?': usage();return 2;break;
			case 'c': clients=atoi(optarg);break;
		}
	}
 	 // printf("-h %s.  %ddfdfdf",headstr,strlen(headlen));
	if(optind==argc)
	{
		fprintf(stderr,"webbench: Missing URL!\n");
		usage();
		return 2;
	}

	if(clients==0) clients=1;
	if(benchtime==0) benchtime=60;
	
	/* Copyright */
	fprintf(stderr,"Webbench - Simple Web Benchmark "PROGRAM_VERSION"\n"
		"Copyright (c) Radim Kolar 1997-2004, GPL Open Source Software.\n"
	);
	
	build_request(argv[optind]);
	
	/* print bench info */
	printf("\nBenchmarking: ");
	switch(method)
	{
		case METHOD_GET:
		default:
			printf("GET");break;
		case METHOD_OPTIONS:
			printf("OPTIONS");break;
		case METHOD_HEAD:
			printf("HEAD");break;
		case METHOD_TRACE:
			printf("TRACE");break;
		case METHOD_POST:
			printf("POST");break;
	}
	
	printf(" %s",argv[optind]);
	switch(http10)
	{
		case 0: printf(" (using HTTP/0.9)");break;
		case 2: printf(" (using HTTP/1.1)");break;
	}
	printf("\n");
	
	if(clients==1) printf("1 client");
	else printf("%d clients",clients);
	printf(", running %d sec", benchtime);
	if(force) printf(", early socket close");
	if(proxyhost!=NULL) printf(", via proxy server %s:%d",proxyhost,proxyport);
	if(force_reload) printf(", forcing reload");
	printf(".\n");

	return bench();
}



int urlencode(char *src, int srclen, char *dst, int dstlen)
{
	int i, j = 0;
	char ch;

	for(i = 0;i < srclen && j < dstlen; i++)
	{
		ch = src[i];
		if((ch >= 'A' && ch <= 'Z') ||
		   (ch >= 'a' && ch <= 'z') ||
		   (ch >= '0' && ch <= '9'))
		{
			dst[j++] = ch;
		}
		else
		if(' ' == ch)
		{
			dst[j++] = '+';
		}
		else
		if('.' == ch || '-' == ch || '_' == ch || '*' == ch || '=' == ch)
		{
			dst[j++] = ch;
		}
		else
		{
			if(j + 3  < dstlen)
			{
				snprintf(dst + j, dstlen - j, "%%%02X", (unsigned char )ch);
				j += 3;
			}
			else
			{
				return -1;
			}
		}
	}
	dst[j] = '\0';
	return 0;
}

int urlencodeall(char *src, int srclen, char *dst, int dstlen)
{
	return 0;
}

int get_postdata4file(char *filename)
{
	int fd, offset, line, maxsize;
	struct stat st;
	void *ptr;
	char *ch, *br = NULL;
	
	fd = open(filename, O_RDONLY);
	if(-1 == fd)
		return -1;

	if(-1 == fstat(fd, &st) || 0 == st.st_size)
		return -1;

	
	ptr = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
	if(!ptr)
		return -1;

	ch = (char *)ptr;
	for(maxsize = 0, line = 0, offset = 0; offset < st.st_size + 1; offset++)
	{
		if(offset > 0 && (*ch == '\n' || *ch == '\0'))
		{
			if(!br) 
				maxsize = offset;
			else if(maxsize < ch - br)
				maxsize = ch - br;
			br = ch;
			line++;
		}
		ch++;
	}

	if(line < 1 && line > 20000)
		return -1;
	
	// printf("line : %d, maxsize : %d\n", line, maxsize);

	int n = 0;
	char *data = calloc(POSTDATA_SIZE, line);
	if(!data)
		return -1;

	ch = (char *)ptr;
	br = NULL;
	
	
	// printf("%s",ch);
	for(offset = 0; offset < st.st_size + 1; offset++)
	{
		
		if(offset > 0 && (*ch == '\n' || *ch == '\0'))
		{
			if(!br){ 
				if(offset > 1)
				{
					memcpy(data, ch-offset, offset);
					*(data+offset) = '\0';
					n++;
				}
			}
			else
			{
				if((ch - br) > 1)
				{
					memcpy(data+n*POSTDATA_SIZE, br+1, ch-br);
					*(data+n*POSTDATA_SIZE+(ch-br)-1) = '\0';
					n++;
				}
			}
			br = ch;
		}
		ch++;
	}

	// int i;
	// for(i = 0; i < n; i++)
	// {
	// 	printf("%s\n", data+i*POSTDATA_SIZE);
	// }
	//
	munmap(ptr, st.st_size);
	close(fd);

	postdataall = data;
	postdataallline = n;
	
	return 0;
}

int get_headdata4file(char *filename)
{
	int fd, offset, line, maxsize;
	struct stat st;
	void *ptr;
	char *ch, *br = NULL;
	
	fd = open(filename, O_RDONLY);
	if(-1 == fd)
		return -1;

	if(-1 == fstat(fd, &st) || 0 == st.st_size)
		return -1;

	
	ptr = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
	if(!ptr)
		return -1;

	ch = (char *)ptr;
	for(maxsize = 0, line = 0, offset = 0; offset < st.st_size + 1; offset++)
	{
		if(offset > 0 && (*ch == '\n' || *ch == '\0'))
		{
			if(!br) 
				maxsize = offset;
			else if(maxsize < ch - br)
				maxsize = ch - br;
			br = ch;
			line++;
		}
		ch++;
	}

	if(line < 1 && line > 20000)
		return -1;
	
	// printf("line : %d, maxsize : %d\n", line, maxsize);

	int n = 0;
	char *data = calloc(HEADDATA_SIZE, line);
	if(!data)
		return -1;

	ch = (char *)ptr;
	br = NULL;
	
	
	// printf("%s",ch);
	for(offset = 0; offset < st.st_size + 1; offset++)
	{
		
		if(offset > 0 && (*ch == '\n' || *ch == '\0'))
		{
			if(!br){ 
				if(offset > 1)
				{
					memcpy(data, ch-offset, offset);
					*(data+offset) = '\0';
					n++;
				}
			}
			else
			{
				if((ch - br) > 1)
				{
					memcpy(data+n*HEADDATA_SIZE, br+1, ch-br);
					*(data+n*HEADDATA_SIZE+(ch-br)-1) = '\0';
					n++;
				}
			}
			br = ch;
		}
		ch++;
	}

	// int i;
	// for(i = 0; i < n; i++)
	// {
	// 	printf("%s\n", data+i*HEADDATA_SIZE);
	// }
	//
	munmap(ptr, st.st_size);
	close(fd);

	headdataall = data;
	headdataallline = n;
	
	return 0;
}

void build_request(const char *url)
{
	char tmp[10];
	int i;

	bzero(host,MAXHOSTNAMELEN);
	bzero(request,REQUEST_SIZE);

	if(force_reload && proxyhost!=NULL && http10<1) http10=1;
	if(method==METHOD_HEAD && http10<1) http10=1;
	if(method==METHOD_OPTIONS && http10<2) http10=2;
	if(method==METHOD_TRACE && http10<2) http10=2;
	if(method==METHOD_POST && http10<2) http10=2;

	switch(method)
	{
		default:
		case METHOD_GET: strcpy(request,"GET");break;
		case METHOD_HEAD: strcpy(request,"HEAD");break;
		case METHOD_OPTIONS: strcpy(request,"OPTIONS");break;
		case METHOD_TRACE: strcpy(request,"TRACE");break;
		case METHOD_POST: strcpy(request,"POST");break;
	}

	strcat(request," ");

	if(NULL==strstr(url,"://"))
	{
		fprintf(stderr, "\n%s: is not a valid URL.\n",url);
		exit(2);
	}
	if(strlen(url)>1500)
	{
		fprintf(stderr,"URL is too long.\n");
		exit(2);
	}
	if(proxyhost==NULL)
	{
		if (0!=strncasecmp("http://",url,7)) 
		{
			fprintf(stderr,"\nOnly HTTP protocol is directly supported, set --proxy for others.\n");
			exit(2);
		}
	}
	/* protocol/host delimiter */
	i=strstr(url,"://")-url+3;
	/* printf("%d\n",i); */

	if(strchr(url+i,'/')==NULL)
	{
		fprintf(stderr,"\nInvalid URL syntax - hostname don't ends with '/'.\n");
		exit(2);
	}
	if(proxyhost==NULL)
	{
		/* get port from hostname */
		if(index(url+i,':')!=NULL && index(url+i,':')<index(url+i,'/'))
		{
			strncpy(host,url+i,strchr(url+i,':')-url-i);
			bzero(tmp,10);
			strncpy(tmp,index(url+i,':')+1,strchr(url+i,'/')-index(url+i,':')-1);
			/* printf("tmp=%s\n",tmp); */
			proxyport=atoi(tmp);
			if(proxyport==0) proxyport=80;
		}
		else
		{
			strncpy(host,url+i,strcspn(url+i,"/"));
		}
		// printf("Host=%s\n",host);
		strcat(request+strlen(request),url+i+strcspn(url+i,"/"));
	} else
	{
		// printf("ProxyHost=%s\nProxyPort=%d\n",proxyhost,proxyport);
		strcat(request,url);
	}
	if(http10==1)
		strcat(request," HTTP/1.0");
	else if (http10==2)
		strcat(request," HTTP/1.1");
	strcat(request,"\r\n");
	if(http10>0)
		strcat(request,"User-Agent: WebBench "PROGRAM_VERSION"\r\n");
	if(proxyhost==NULL && http10>0)
	{
		strcat(request,"Host: ");
		strcat(request,host);
		strcat(request,"\r\n");
	}
	if(force_reload && proxyhost!=NULL)
		strcat(request,"Pragma: no-cache\r\n");
	if(http10>1)
		strcat(request,"Connection: close\r\n");
	
	//这里是处理-h的参数，添加头部的值
	if(headlen > 0){
		/*set head*/
		char *strsplite = headstr;
		char *p;
		// printf("%s \n",strtok(strsplite, ";"));
		strcat(request,strtok(strsplite, ";"));
		strcat(request,"\r\n");
		while((p = strtok(NULL, ";")))
		{
			strcat(request,p);
			strcat(request,"\r\n");
		}
	}

	//处理断言
	if(assertlen>0){
		char *delim = ";";
		char *pch;
		pch = strtok (assertstr,delim);
		assertlen = 0;
		while (pch != NULL)
		{
			assertlist[assertlen] = pch;
			pch = strtok(NULL, delim);
			assertlen++;
		}
	}
//	for (int a=0;a<assertlen;a++){
//		printf("aaaa%s\n",assertlist[a]);
//	}
//在发送的时候在改post的数据，没必要这样做
	/* add post data */
//	if(method==METHOD_POST)
//	{
//		if(postdataall)
//		{
//			int maxlen = postdataallline > headdataallline ? postdataallline : headdataallline;
//			requestall = calloc(POSTDATA_SIZE, maxlen);
//
//			if(!requestall)
//				return;
//
//			for(i = 0; i < maxlen; i++)
//			{
//
//				if(headdataall)
//				{
//					snprintf(requestall+i*POSTDATA_SIZE, POSTDATA_SIZE,
//													"%s"
//													"Accept: */*\r\n"
//													"Content-Length: "
//													"%d"
//													"\r\n"
//													"%s"
//													"\r\n\r\n"
//													"%s",
//													request,
//													strlen(postdataall+(i % postdataallline) * POSTDATA_SIZE),
//													headdataall + (i % headdataallline) * HEADDATA_SIZE,
//													postdataall + (i % postdataallline) * POSTDATA_SIZE);
//													printf("cccc\n%s\n",requestall+i*POSTDATA_SIZE);
//				}else
//				{
//					snprintf(requestall+i*POSTDATA_SIZE, POSTDATA_SIZE,
//													"%s"
//													"Accept: */*\r\n"
//													"Content-Length: "
//													"%d"
//													"\r\n\r\n"
//													"%s",
//													request,
//													strlen(postdataall+(i % postdataallline)*POSTDATA_SIZE),
//													postdataall + (i % postdataallline) * POSTDATA_SIZE);
//													printf("cccc\n%s\n",requestall+i*POSTDATA_SIZE);
//				}
//			}
//			requestallsize = i;
//			clients = i < clients ? i : clients;
//			// printf("clients: %d",clients);
//		}
//		else
//		{
//			strcat(request, "Accept: */*\r\n");
//			strcat(request, "Content-Length: ");
//			strcat(request, postdatalen);
//			strcat(request, "\r\n\r\n");
//			strcat(request, postdata);
//		}
//	}else{
//		//	如果是get请求，如果存在head的文件
//		if(headdataall){
//			int maxlen = headdataallline;
//			requestall = calloc(POSTDATA_SIZE, maxlen);
//
//			if(!requestall)
//				return;
//
//			for(i = 0; i < maxlen; i++)
//			{
//				snprintf(requestall+i*POSTDATA_SIZE, POSTDATA_SIZE,
//							"%s"
//							"Accept: */*\r\n"
//							"%s"
//							"\r\n",
//							request,
//							headdataall + i * HEADDATA_SIZE);
//				printf("cccc\n%s\n",requestall+i*POSTDATA_SIZE);
//			}
//			requestallsize = i;
//			clients = i < clients ? i : clients;
//		}else {
//			if(http10>0) {
//				strcat(request,"\r\n");/* add empty line at end */
//			}
//		}
//	}
	int maxlen = postdataallline > headdataallline ? postdataallline : headdataallline;
	clients = maxlen < clients ? maxlen : clients;
//	printf("Req=%s\n",request);
}

/* vraci system rc error kod */
static int bench(void)
{
	int i,j,k;
	double l,m,n;	
	pid_t pid=0;
	FILE *f;

	/* check avaibility of target server */
	i=Socket(proxyhost==NULL?host:proxyhost,proxyport);
	
	// printf("%s --- %d\n",host,proxyport);
	
	if(i<0) { 
		fprintf(stderr,"\nConnect to server failed. Aborting benchmark.\n");
		return 1;
	}
	close(i);
	/* create pipe */
	if(pipe(mypipe))
	{
		perror("pipe failed.");
		return 3;
	}

	/* not needed, since we have alarm() in childrens */
	/* wait 4 next system clock tick */
	/*
	cas=time(NULL);
	while(time(NULL)==cas)
	sched_yield();
	*/

	/* fork childs */
	for(i=0;i<clients;i++)
	{
		pid=fork();
		if(pid <= (pid_t) 0)
		{
			/* child process or error*/
			sleep(1); /* make childs faster */
			break;
		}
	}

	if( pid< (pid_t) 0)
	{
		fprintf(stderr,"problems forking worker no. %d\n",i);
		perror("fork failed.");
		return 3;
	}

	if(pid== (pid_t) 0)
	{
		/* I am a child */

		char *dsthost = host, *requestdata = request;
//		if(requestall && i < requestallsize)
//			requestdata = requestall+i*POSTDATA_SIZE;

		//动态的数据就动态赋值，这是头部
		if(headdataall){
			strcat(request,headdataall +  (i % headdataallline) * HEADDATA_SIZE);
			strcat(request,"\r\n");
		}
//		printf("cccc\n%s\n",request);
		//动态的数据就动态赋值，这是body
		if(method==METHOD_POST){
			strcat(request, "Accept: */*\r\n");
			strcat(request, "Content-Length: ");

			if(postdataall){
//				printf("cccc\n%d\n",strlen(postdataall+(i % postdataallline) * POSTDATA_SIZE));
				char str[10];
				int len  = strlen(postdataall+(i % postdataallline) * POSTDATA_SIZE);
				sprintf(str,"%d", len);

//				printf("cccc\n%s\n",str);
				strcat(request, str);
				strcat(request, "\r\n\r\n");
				strcat(request, postdataall + (i % postdataallline) * POSTDATA_SIZE);
			}else{
				char strlen[10];
				sprintf(strlen,"%d", postdatalen);

				strcat(request, strlen);
				strcat(request, "\r\n\r\n");
				strcat(request, postdata);
			}
		}

//		printf("cccc\n%s\n",request);

		if(http10>0) {
			strcat(request,"\r\n");/* add empty line at end */
		}

		if(proxyhost)
			dsthost = proxyhost;

		printf("req=%s\n",requestdata);

		benchcore(dsthost,proxyport,requestdata);

		/* write results to pipe */
		f=fdopen(mypipe[1],"w");
		if(f==NULL)
		{
			perror("open pipe for writing failed.");
			return 3;
		}
		/* fprintf(stderr,"Child - %d %d\n",speed,failed); */
		fprintf(f,"%d %d %d %lf %lf %lf\n",speed,failed,bytes,min_time,max_time,all_time);
		fclose(f);
		return 0;
	} else
	{
		f=fdopen(mypipe[0],"r");
		if(f==NULL) 
		{
			perror("open pipe for reading failed.");
			return 3;
		}
		setvbuf(f,NULL,_IONBF,0);
		speed=0;
		failed=0;
		bytes=0;

		while(1)
		{
			pid=fscanf(f,"%d %d %d %lf %lf %lf",&i,&j,&k,&l,&m,&n);
			if(pid<2)
			{
				fprintf(stderr,"Some of our childrens died.\n");
				break;
			}
			speed+=i;
			failed+=j;
			bytes+=k;
			
			set_all_time(l,m,n);
			/* fprintf(stderr,"*Knock* %d %d read=%d\n",speed,failed,pid); */
			if(--clients==0) break;
		}
		fclose(f);

		printf("\nSpeed=%d pages/sec, %d bytes/sec.\nRequests: %d susceed, %d failed.\n",
				(int)((speed+failed)/(benchtime)),
				(int)(bytes/(float)benchtime),
				speed,
				failed);
		printf("min_time=%0.2fms , max_time=%0.2fms, ave_time=%0.2fms all_time=%0.2fms .\n",min_time,max_time,all_time/(speed+failed),all_time);
	}
	return i;
}

void mark_time(clock_t start,clock_t finish)
{
	double duration;
	duration = (double)(finish - start);
	min_time = duration < min_time ? duration : min_time;
	max_time = duration > max_time ? duration : max_time;
	all_time += duration;
}

static void set_all_time(double min,double max,double all)
{
	min_time = min < min_time ? min : min_time;
	max_time = max > max_time ? max : max_time;
	all_time += all;
}

void benchcore(const char *host,const int port,const char *req)
{
	int rlen;
	char buf[4096];
	int s,i;
	struct sigaction sa;
	
	/*init time*/
	clock_t start, finish;
	
	/* setup alarm signal handler */
	sa.sa_handler=alarm_handler;
	sa.sa_flags=0;
	if(sigaction(SIGALRM,&sa,NULL))
		exit(3);
	alarm(benchtime);

	rlen=strlen(req);
	nexttry:while(1)
	{
		memset(buf,0,4096);
		if(timerexpired)
		{
			if(failed>0)
			{
				/* fprintf(stderr,"Correcting failed by signal\n"); */
				failed--;
			}
			return;
		}

		s=Socket(host,port);                          
		if(s<0) { failed++;continue;}
		
		start = clock();

		if(rlen!=write(s,req,rlen)) {failed++;close(s);continue;}
		
		
		if(http10==0) 
			if(shutdown(s,1)) { failed++;close(s);continue;}
		if(force==0)
		{
			/* read all available data from socket */
			while(1)
			{
				if(timerexpired)
					break;
				i=read(s,buf,4096);
				
//				/* fprintf(stderr,"%d\n",i); */
//				printf("%s\n",buf);
				if(i<0) 
				{ 
					//计算时间
					finish = clock();
					mark_time(start,finish);

					failed++;
					close(s);
					goto nexttry;
				}
				else if(i==0) break;
				else bytes+=i;
			}

			//计算时间
			finish = clock();
			mark_time(start,finish);
		}
		if(close(s)) {failed++;continue;}

		if (assertlen > 0){
			if (assertrsp(buf)){
//				printf("断言正确");
				speed++;
			}else{
//				printf("断言错误");
				failed++;
			}
		}else{
			speed++;
		}
	}
}

int  assertrsp(char *rsp){
	if(assertlen > 0){
		for(int i=0;i<assertlen;i++){
			char *check;
			check = strstr(rsp,assertlist[i]);
			if (NULL==check){
				return 0;
			}
		}
	}
	return 1;
}

