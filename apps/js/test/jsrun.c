// native test harness for jshost: jsrun URL [commands...]
// commands: pump:N  render  click:AID  input:AID:text  key:AID:Key  console  wait:MS  dump
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "jsplatform.h"
#include "jshost.h"

uint64_t jsp_time_ms(void){ struct timespec t; clock_gettime(CLOCK_REALTIME,&t); return (uint64_t)t.tv_sec*1000+t.tv_nsec/1000000; }
void jsp_log(int l,const char*t){ fprintf(stderr,"[log%d] %s\n",l,t); }
void jsp_announce(const char*t){ fprintf(stderr,"[announce] %s\n",t); }
void jsp_random(void*b,int n){ FILE*f=fopen("/dev/urandom","rb"); if(f){ fread(b,1,n,f); fclose(f);} }
int jsp_width(void){return 800;} int jsp_height(void){return 600;} int jsp_theme(void){return 0;}

static int resolve(const char *base, const char *url, char *out, int max){
    if(!strncmp(url,"http://",7)||!strncmp(url,"https://",8)){ snprintf(out,max,"%s",url); return 0; }
    return -1;
}
int jsp_http(const char*method,const char*url,const char*headers,const char*body,int body_len,int timeout_ms,struct jsp_response*out){
    char u[2048]; if(resolve(0,url,u,sizeof u)){ snprintf(out->error,sizeof out->error,"bad url %s",url); return 1; }
    if(!strncmp(u,"https://",8)){ snprintf(out->error,sizeof out->error,"https unsupported in harness"); return 1; }
    char host[256]; int port=80; const char*p=u+7; const char*slash=strchr(p,'/'); char hp[300];
    int hl = slash? (int)(slash-p):(int)strlen(p); snprintf(hp,sizeof hp,"%.*s",hl,p);
    char *colon=strchr(hp,':'); if(colon){*colon=0; port=atoi(colon+1);} snprintf(host,sizeof host,"%s",hp);
    const char*path=slash?slash:"/";
    struct hostent*he=gethostbyname(host); if(!he){snprintf(out->error,sizeof out->error,"dns");return 1;}
    int s=socket(AF_INET,SOCK_STREAM,0); struct sockaddr_in a={0}; a.sin_family=AF_INET;a.sin_port=htons(port); memcpy(&a.sin_addr,he->h_addr_list[0],4);
    if(connect(s,(void*)&a,sizeof a)){snprintf(out->error,sizeof out->error,"connect");close(s);return 1;}
    char req[8192]; int n=snprintf(req,sizeof req,"%s %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n%s",method,path,hp[0]?host:"",headers?headers:"");
    if(body_len) n+=snprintf(req+n,sizeof req-n,"Content-Length: %d\r\n",body_len);
    n+=snprintf(req+n,sizeof req-n,"\r\n"); write(s,req,n); if(body_len) write(s,body,body_len);
    size_t cap=1<<16,len=0; char*buf=malloc(cap); int r;
    while((r=read(s,buf+len,cap-len-1))>0){ len+=r; if(len+1>=cap){cap*=2;buf=realloc(buf,cap);} }
    close(s); buf[len]=0;
    char*he_=strstr(buf,"\r\n\r\n"); if(!he_){snprintf(out->error,sizeof out->error,"bad response");free(buf);return 1;}
    out->status=atoi(buf+9); char*sp=strchr(buf+9,' '); if(sp){ char*e=strchr(sp+1,'\r'); snprintf(out->status_text,sizeof out->status_text,"%.*s",(int)(e-sp-1),sp+1);}
    char*hs=strchr(buf,'\n')+1; out->headers_len=(int)(he_-hs)+2; out->headers=malloc(out->headers_len+1); memcpy(out->headers,hs,out->headers_len); out->headers[out->headers_len]=0;
    char*b=he_+4; size_t bl=len-(b-buf);
    if(strcasestr(out->headers,"transfer-encoding: chunked")){
        char*o=malloc(bl+1); size_t ol=0; char*q=b; while(q<b+bl){ long c=strtol(q,&q,16); q=strstr(q,"\r\n"); if(!q)break; q+=2; if(c==0)break; memcpy(o+ol,q,c); ol+=c; q+=c+2; }
        out->body=o; out->body_len=ol;
    } else { out->body=malloc(bl+1); memcpy(out->body,b,bl); out->body_len=bl; }
    snprintf(out->final_url,sizeof out->final_url,"%s",u); free(buf);
    if(out->status>=300&&out->status<400){ /* redirects: left to caller */ }
    return 0;
}

int main(int argc,char**argv){
    struct jsp_response r={0};
    if(argc<2) return 2;
    if(jsp_http("GET",argv[1],"Accept: text/html\r\n",0,0,10000,&r)){ fprintf(stderr,"fetch failed: %s\n",r.error); return 1; }
    if(!jsh_page_start(argv[1],r.body,r.body_len)){ fprintf(stderr,"start failed\n%s\n",jsh_console()); return 1; }
    for(int i=2;i<argc;i++){
        char*c=argv[i];
        if(!strncmp(c,"pump:",5)){ int n=atoi(c+5); for(int k=0;k<n;k++){ int f=jsh_pump(); if(f&JSH_NAV) printf("[NAV %s]\n",jsh_nav_url()); if(f&JSH_URL){int m; printf("[URL %s %d]\n",jsh_url(&m),m);} if(f&JSH_TITLE) printf("[TITLE %s]\n",jsh_title()); const char*s; while((s=jsh_announcement())) printf("[SAY %s]\n",s); if(f&JSH_FATAL){printf("[FATAL]\n");break;} usleep(20000);} }
        else if(!strcmp(c,"full")){ int l; const char*s=jsh_render_full(&l); printf("%s\n",s); }
        else if(!strcmp(c,"render")){ int l; const char*s=jsh_render(&l); printf("%s\n",s); }
        else if(!strncmp(c,"click:",6)) printf("[click %d]\n",jsh_click(atoi(c+6)));
        else if(!strncmp(c,"input:",6)){ int aid=atoi(c+6); char*t=strchr(c+6,':'); jsh_input(aid,t?t+1:""); }
        else if(!strncmp(c,"key:",4)){ int aid=atoi(c+4); char*t=strchr(c+4,':'); jsh_key(aid,t?t+1:""); }
        else if(!strcmp(c,"console")) printf("--console--\n%s\n",jsh_console());
        else if(!strncmp(c,"wait:",5)) usleep(atoi(c+5)*1000);
    }
    return 0;
}
