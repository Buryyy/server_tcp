
/*
* Entry point of our server. 
*/

#define SRC_MAIN_SERVER_C_
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <bb_main.h>

int main()
{
    // Initializations:
    int i, j;                      // For general use.
    pthread_t thread;              // Main thread ID (myself).
    cpu_set_t cpuset;              // Each bit represents a CPU.
    struct sockaddr_in srvaddr;    // IPv4 socket address structure.

    // Set config defaults:
    s.cnf.ehint = EPOLL_HINT;
    s.cnf.epoev = EPOLL_EVENTS;
    s.cnf.athre = ACCEPT_THREADS;
    s.cnf.dthre = DATA_THREADS;
    s.cnf.tcpnd = TCP_NDELAY;

    // Parse command line options:
    struct option longopts[] = {
    { "epoll-hint",     required_argument,  NULL,  'h' },
    { "epoll-events",   required_argument,  NULL,  'e' },
    { "accept-threads", required_argument,  NULL,  'a' },
    { "data-threads",   required_argument,  NULL,  'd' },
    { "tcp-nodelay",    no_argument,        NULL,  'n' },
    { 0, 0, 0, 0 }};

    while((i = getopt_long(argc, argv, "h:e:a:d:n", longopts, NULL)) != -1)

    {
        if (i == -1) break;

        switch(i)

        {
            case 'h': s.cnf.ehint = atoi(optarg);
                      break;
            case 'e': s.cnf.epoev = atoi(optarg);
                      break;
            case 'a': s.cnf.athre = atoi(optarg);
                      break;
            case 'd': s.cnf.dthre = atoi(optarg);
                      break;
            case 'n': s.cnf.tcpnd = 1;
                      break;
            default:  abort();
        }
    }

    // Daemonize:
    daemonize();

    // Initialize server structures:
    s.cores = sysconf(_SC_NPROCESSORS_ONLN);
    if((s.epfd = malloc(sizeof(int) * s.cores)) == NULL) MyDBG(end0);
    if(bb_fifo_new(&s.fifo) < 0) MyDBG(end1);

    // Server blocking socket. Go ahead and reuse it:
    if((s.srvfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) MyDBG(end1);
    i=1; if(setsockopt(s.srvfd, SOL_SOCKET, SO_REUSEADDR, &i, sizeof(i)) < 0) MyDBG(end2);

    // Initialize srvaddr:
    bzero(&srvaddr, sizeof(srvaddr));
    srvaddr.sin_family = AF_INET;
    srvaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    srvaddr.sin_port = htons(LISTENP);

    // Bind and listen:
    if(bind(s.srvfd, (struct sockaddr *) &srvaddr, sizeof(srvaddr)) < 0) MyDBG(end2);
    if(listen(s.srvfd, LISTENQ) < 0) MyDBG(end2);

    // For each core in the system:
    for(i=0; i<s.cores; i++)

    {
        // Open an epoll fd dimensioned for s.cnf.ehint/s.cores descriptors:
        if((s.epfd[i] = epoll_create(s.cnf.ehint/s.cores)) < 0) MyDBG(end2);

        // Wait-Worker inherits a copy of its creator's CPU affinity mask:
        CPU_ZERO(&cpuset); CPU_SET(i, &cpuset);
        if(pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset) != 0) MyDBG(end2);
        if(pthread_create(&thread, NULL, W_Wait, (void *)(intptr_t)s.epfd[i]) != 0) MyDBG(end2);
    }

    // Round-Robin distribution of Accept-Workers among all available cores:
    for (j=0; j<s.cnf.athre; j++){for (i=0; i<s.cores; i++)

    {
        // Accept-Worker inherits a copy of its creator's CPU affinity mask:
        CPU_ZERO(&cpuset); CPU_SET(i, &cpuset);
        if(pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset) != 0) MyDBG(end2);
        if(pthread_create(&thread, NULL, W_Acce, (void *)(intptr_t)s.epfd[i]) != 0) MyDBG(end2);
    }}

    // Restore creator's (myself) affinity to all available cores:
    CPU_ZERO(&cpuset); for(i=0; i<s.cores; i++){CPU_SET(i, &cpuset);}
    if(pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset) != 0) MyDBG(end2);

    // Pre-threading a pool of s.cnf.dthre Data-Workers:
    for(i=0; i<s.cnf.dthre; i++){if(pthread_create(&thread, NULL, W_Data, NULL) != 0) MyDBG(end2);}

    // Register a signal handler for SIGINT (Ctrl-C)
    if((signal(SIGINT, sig_int)) == SIG_ERR) MyDBG(end2);

    // Loop forever:
    while(1){sleep(10);}

    // Return on error:
    end2: close(s.srvfd);
    end1: free(s.epfd);
    end0: return -1;
    return 0;
}
