#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
 #include <unistd.h>
#include <libwebsockets.h>
#include <sys/time.h>  
#define KGRN "\033[0;32;32m"
#define KCYN "\033[0;36m"
#define KRED "\033[0;32;31m"
#define KYEL "\033[1;33m"
#define KBLU "\033[0;32;34m"
#define KCYN_L "\033[1;36m"
#define KBRN "\033[0;33m"
#define RESET "\033[0m"
#define EXAMPLE_RX_BUFFER_BYTES (100)


 static void connect_client(lws_sorted_usec_list_t *sul);
static int interrupted;

// helper functions
void getValueOfKey( char* result, char dataSimple[], char keyName[] );
void writeFormattedDataToFile(char fileName[],  char key[], char value[]);
void calculateCandleStickDataForMinuteAndSaveThemToFile(char *symbolName);

// t1,t2 and elapsedTime to note when a minute has passed
struct timeval t1, t2;  

static void INT_HANDLER(int signo) {
    destroy_flag = 1;
}

struct session_data {
    int fd;
};

struct pthread_routine_tool {
    struct lws_context *context;
    struct lws *wsi;
};  


static struct lws_context *context;
static int destroy_flag = 0;
static int connection_flag = 0;
static int writeable_flag = 0;  

// The retry and backoff policy we want to use for our client connections 
static struct my_conn {
	lws_sorted_usec_list_t	sul;  /* schedule connection retry */
	struct lws		*wsi;	     /* related wsi if any */
	uint16_t		retry_count; /* count of consequetive retries */
} mco; 

static void connect_client(lws_sorted_usec_list_t *sul);
static const uint32_t backoff_ms[] = { 1000, 2000, 3000, 4000, 5000 };
static const lws_retry_bo_t retry = {
	.retry_ms_table			= backoff_ms,
	.retry_ms_table_count		= LWS_ARRAY_SIZE(backoff_ms),
	.conceal_count			= LWS_ARRAY_SIZE(backoff_ms),
	.secs_since_valid_ping		= 3,  /* force PINGs after secs idle */
	.secs_since_valid_hangup	= 10, /* hangup after secs idle */ 
	.jitter_percent			= 20,
}; 

static int ws_service_callback( struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in, size_t len);
lws_sorted_usec_list_t	sortedUsecListT;
static const struct lws_protocols protocols[] = {
	{ "lws-minimal-client", ws_service_callback, 0, 0, 0, NULL, 0 }, LWS_PROTOCOL_LIST_TERM
}; 

// The dequeues here hold the sum of stocks for each minute and sum of volumes for each minute, for each stock.
// Each cell of the stocksPerMinute dequeue contains the sum of stocks that were found in each minute. 
// The volume dequeues, hold the sum of volumes the same way.
// The front of the dequeu has the newest minute and the end of the dequeue has the oldest minute.
// When 15 minute have passed, we remove from our moving mean and volume sum, the end of the dequeue (which is for the oldest minute).   
#define MAX_SIZE_OF_DEQUEUE 15 // size of dequeue-> max number of minutes the stock info is hold
// for mean
double stocksPerMinuteDequeueAmzn[MAX_SIZE_OF_DEQUEUE];
double stocksPerMinuteDequeueIbm[MAX_SIZE_OF_DEQUEUE];
double stocksPerMinuteDequeueBinance[MAX_SIZE_OF_DEQUEUE]; 
int frontOfDequeueAmzn= -1, rearOfDequeueAmzn=-1 ;  
int frontOfDequeueIbm= -1, rearOfDequeueIbm=-1 ;  
int frontOfDequeueBinance= -1, rearOfDequeueBinance=-1 ; 

// for volumes 
double volumesPerMinuteDequeueAmzn[MAX_SIZE_OF_DEQUEUE];
double volumesPerMinuteDequeueIbm[MAX_SIZE_OF_DEQUEUE];
double volumesPerMinuteDequeueBinance[MAX_SIZE_OF_DEQUEUE]; 

int frontOfVolumeDequeueAmzn= -1, rearOfVolumeDequeueAmzn=-1 ;  
int frontOfVolumeDequeueIbm= -1, rearOfVolumeDequeueIbm=-1 ;  
int frontOfVolumeDequeueBinance= -1, rearOfVolumeDequeueBinance=-1 ;  

// helper functions for dequeues

void addToFrontOfDequeue(double *, double, int *, int *);
void addToBackOfDequeue(double *, double, int *, int *);
void updateExistingFrontElementOfDequeue(double *, int, int *, int * );
double delFrontOfDequeue(double *, int *, int *);
double delBackOfDequeue(double *, int *, int *);
void displayDequeue(double *);
int countElementsInDequeue(double *);
double getSumOfDequeuesValues(double *arr);

// when a minute has passed,we write down in the dequeue that the next stocks 
// will belong to a new minute , that will belong in a "new cell" of the dequeue
void moveDequeueToNextMinute();

 
clock_t receivedData;

int countElementsInDequeue(double *arr) {
  int c = 0, i;

  for (i = 0; i < MAX_SIZE_OF_DEQUEUE; i++) {
    if (arr[i] != 0)
      c++;
  }
  return c;
}

void displayDequeue(double *arr) { 
  printf("\nfront:  ");
  for (int i = 0; i < MAX_SIZE_OF_DEQUEUE; i++)  printf("  %f", arr[i]);
  printf("  :end\n");
}

double getSumOfDequeuesValues(double *arr) { 
  double sum =0;

  for (int i = 0; i < MAX_SIZE_OF_DEQUEUE; i++) sum += arr[i];
  return sum;
}

static int ws_service_callback( struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in, size_t len)
{  
    struct my_conn *m = (struct my_conn *)user; 
    switch (reason) { 
        case LWS_CALLBACK_CLIENT_ESTABLISHED:
            printf(KYEL"[Main Service] Connect with server success.\n"RESET);
            connection_flag = 1;
	    lws_callback_on_writable(wsi);
            break; 
        case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
            printf(KRED"[Main Service] Connect with server error: %s.\n"RESET, in);
            lwsl_err(KRED"[Main Service] Connect with server error: %s.\n"RESET, in);
            destroy_flag = 1;
            connection_flag = 0;
            goto do_retry;   
        case LWS_CALLBACK_CLOSED:
            printf(KYEL"[Main Service] LWS_CALLBACK_CLOSED\n"RESET);
            destroy_flag = 1;
            connection_flag = 0;
            break; 
        case LWS_CALLBACK_CLIENT_RECEIVE:
        { 
            receivedData = clock();
            char *content = (char *)in;
            if (strstr(content, "ping") != NULL) {
                lwsl_notice("Received a simple ping %s. We skip that.\n",  (char *)in);
            }else{
                 writeDataToFile((char*)in);   
            } 
            break;
            }
        case LWS_CALLBACK_CLIENT_WRITEABLE :
            printf(KYEL"[Main Service] On writeable is called.\n"RESET);  
            subscribeToSymbol(wsi,"BINANCE:BTCUSDT");
            subscribeToSymbol(wsi,"APPL");
            subscribeToSymbol(wsi,"AMZN");
            subscribeToSymbol(wsi,"IC MARKETS:1");
            subscribeToSymbol(wsi,"IBM"); 
            writeable_flag = 1;
            break;
        case LWS_CALLBACK_CLIENT_CLOSED:
            printf(KYEL"\n[Main Service] LWS_CALLBACK_CLIENT_CLOSED is called\n"RESET);  
            goto do_retry; 
        default:
            break;
    }
     return lws_callback_http_dummy(wsi, reason, user, in, len);
 
/*  retry the connection to keep it nailed up   */
  do_retry: { 
      struct lws_client_connect_info clientConnectionInfo;
      time_t rawtime;
      struct tm * timeinfo;
      char timeBuffer[80];
      time ( &rawtime );
      timeinfo = localtime ( &rawtime );
      strftime(timeBuffer,80,"%x - %I:%M%p", timeinfo); 
      memset(&clientConnectionInfo, 0, sizeof(clientConnectionInfo));
      clientConnectionInfo.context = context; 

        char inputURL[300] = "ws.finnhub.io/?token=ca5l84iad3i4sbn0eskg";
        const char *urlProtocol, *urlTempPath;
        char urlPath[300];
        if (lws_parse_uri(inputURL, &urlProtocol, &clientConnectionInfo.address,  &clientConnectionInfo.port, &urlTempPath)) printf("Couldn't parse URL\n");
              
        urlPath[0] = '/';
        strncpy(urlPath + 1, urlTempPath, sizeof(urlPath) - 2);
        urlPath[sizeof(urlPath)-1] = '\0';
        clientConnectionInfo.path = urlPath;  
        clientConnectionInfo.port = 443;
        clientConnectionInfo.host = clientConnectionInfo.address;
        clientConnectionInfo.origin = clientConnectionInfo.address;
        clientConnectionInfo.ssl_connection =  LCCSCF_USE_SSL | LCCSCF_ALLOW_SELFSIGNED | LCCSCF_SKIP_SERVER_CERT_HOSTNAME_CHECK;
        clientConnectionInfo.protocol = protocols[0].name;
        clientConnectionInfo.local_protocol_name = protocols[0].name;  
        clientConnectionInfo.retry_and_idle_policy = &retry; 
        clientConnectionInfo.ietf_version_or_minus_one = -1;  
        lws_sul_schedule(context, 0, &mco.sul, connect_client, 1);
        lws_service(context, 0);    
  }
}


void initializeDequeues(){  
  for (int i = 0; i < MAX_SIZE_OF_DEQUEUE; i++){ 
        // for mean
        stocksPerMinuteDequeueAmzn[i] = 0;
        stocksPerMinuteDequeueIbm[i] = 0; 
        stocksPerMinuteDequeueBinance[i] = 0; 
        // for volume
        volumesPerMinuteDequeueAmzn[i] = 0;
        volumesPerMinuteDequeueIbm[i] = 0; 
        volumesPerMinuteDequeueBinance[i] = 0; 
  }  
}

double delBackOfDequeue(double *arr, int *pfront, int *prear) {
        double item;

        if (*pfront == -1)  return 0; 

        item = arr[*prear];
        arr[*prear] = 0;
        (*prear)--;
        if (*prear == -1)
        *pfront = -1;
        return item;
}

double delFrontOfDequeue(double *arr, int *pfront, int *prear) {
        double item; 
        if (*pfront == -1)  return 0; 
        item = arr[*pfront];
        arr[*pfront] = 0;

        if (*pfront == *prear)
        *pfront = *prear = -1;
        else  (*pfront)++;

        return item;
}
void updateExistingFrontElementOfDequeue(double *arr, int valueToAdd, int *pfront, int *prear ){
        double frontElement = delFrontOfDequeue(arr,  pfront,  prear);  
        double newElement = frontElement +valueToAdd;  
        addToFrontOfDequeue(arr,newElement,  pfront, prear); 
}


void addToFrontOfDequeue(double *arr, double item, int *pfront, int *prear) {
        int i, k, c;

        if (*pfront == 0 && *prear == MAX_SIZE_OF_DEQUEUE - 1) return;
        

        if (*pfront == -1) {
                *pfront = *prear = 0;
                arr[*pfront] = item;
                return;
        }

        if (*prear != MAX_SIZE_OF_DEQUEUE - 1) {
        c = countElementsInDequeue(arr);
        k = *prear + 1;
        for (i = 1; i <= c; i++) {
        arr[k] = arr[k - 1];
        k--;
        }
        arr[k] = item;
        *pfront = k;
        (*prear)++;
        } else {
        (*pfront)--;
        arr[*pfront] = item;
        }
}

void addToBackOfDequeue(double *arr, double item, int *pfront, int *prear) {
  int i, k; 
  if (*pfront == 0 && *prear == MAX_SIZE_OF_DEQUEUE - 1)  return;
  
  if (*pfront == -1) {
    *prear = *pfront = 0;
    arr[*prear] = item;
    return;
  }

  if (*prear == MAX_SIZE_OF_DEQUEUE - 1) {
    k = *pfront - 1;
    for (i = *pfront - 1; i < *prear; i++) {
      k = i;
      if (k == MAX_SIZE_OF_DEQUEUE - 1)
        arr[k] = 0;
      else
        arr[k] = arr[i + 1];
    }
    (*prear)--;
    (*pfront)--;
  }
  (*prear)++;
  arr[*prear] = item;
} 
 
static int websocket_write_back(struct lws *wsi_in, char *str, int str_size_in) 
{
    if (str == NULL || wsi_in == NULL) return -1;

    int n;
    int len;
    char *out = NULL;

    if (str_size_in < 1)   len = strlen(str);
    else len = str_size_in;

    out = (char *)malloc(sizeof(char)*(LWS_SEND_BUFFER_PRE_PADDING + len + LWS_SEND_BUFFER_POST_PADDING));
    //* setup the buffer*/
    memcpy (out + LWS_SEND_BUFFER_PRE_PADDING, str, len );
    //* write out*/
    n = lws_write(wsi_in, out + LWS_SEND_BUFFER_PRE_PADDING, len, LWS_WRITE_TEXT);

    printf(KBLU"[websocket_write_back] %s\n"RESET, str);
    
    return n;
} 

void calculateCandleStickDataForMinuteAndSaveThemToFile(char *symbolName){
           // nohup ./candlestick AMZN 1572651390 1575243390 & 
           char array[255]= " nohup ./candlestick "; 
           strcat(array,  symbolName); 
           strcat(array,  " "); 
           char startingStringTimestamp[50];  
           char endingStringTimestamp[50];  
          
            snprintf(startingStringTimestamp, 50, "%d", t1.tv_sec); 
            snprintf(endingStringTimestamp, 50, "%d", t2.tv_sec); 
            strcat(array,   startingStringTimestamp);
            strcat(array,   " ");
            strcat(array,  endingStringTimestamp);  
            system(array);  
            system("rm nohup.out");
}

void resetTimers(){  
    gettimeofday(&t2, NULL);
    gettimeofday(&t1, NULL);  
} 

void checkIfMoreThan15MinPassedAndDeleteOldestMinute(){
        int minuteToDelete = 15;
        // this function deletes oldest stocks if more than 15 min passed
        if(countElementsInDequeue(stocksPerMinuteDequeueAmzn)>=minuteToDelete){
            delBackOfDequeue(stocksPerMinuteDequeueAmzn, &frontOfDequeueAmzn,&rearOfDequeueAmzn);   
            delBackOfDequeue(volumesPerMinuteDequeueAmzn, &frontOfVolumeDequeueAmzn,&rearOfVolumeDequeueAmzn);   
        } 

        if(countElementsInDequeue(stocksPerMinuteDequeueIbm)>= minuteToDelete){
          delBackOfDequeue(stocksPerMinuteDequeueIbm, &frontOfDequeueIbm,&rearOfDequeueIbm); 
          delBackOfDequeue(volumesPerMinuteDequeueIbm, &frontOfVolumeDequeueIbm,&rearOfVolumeDequeueIbm); 
        }        
        
        if(countElementsInDequeue(stocksPerMinuteDequeueBinance)>= minuteToDelete)  { 
                delBackOfDequeue(stocksPerMinuteDequeueBinance, &frontOfDequeueBinance,&rearOfDequeueBinance);    
                delBackOfDequeue(volumesPerMinuteDequeueBinance, &frontOfVolumeDequeueBinance,&rearOfVolumeDequeueBinance);   
        }
        
}

 void moveDequeueToNextMinute(){ 
       checkIfMoreThan15MinPassedAndDeleteOldestMinute(); 
        // we found one stock in a "new minute". We go to a new cell in our dequeues to store the data for the new minute
        // for means
        addToFrontOfDequeue(stocksPerMinuteDequeueAmzn,0, &frontOfDequeueAmzn,&rearOfDequeueAmzn);  
        addToFrontOfDequeue(stocksPerMinuteDequeueIbm,0, &frontOfDequeueIbm,&rearOfDequeueIbm);  
        addToFrontOfDequeue(stocksPerMinuteDequeueBinance,0, &frontOfDequeueBinance,&rearOfDequeueBinance);  

        // for volumes
        addToFrontOfDequeue(volumesPerMinuteDequeueAmzn,0, &frontOfVolumeDequeueAmzn,&rearOfVolumeDequeueAmzn);  
        addToFrontOfDequeue(volumesPerMinuteDequeueIbm,0, &frontOfVolumeDequeueIbm,&rearOfVolumeDequeueIbm);  
        addToFrontOfDequeue(volumesPerMinuteDequeueBinance,0, &frontOfVolumeDequeueBinance,&rearOfVolumeDequeueBinance);  
 }


void writeDataToFile(char data[]){ 
        char symbolKey[] = "\"s\":";  
        char * value  =(char *)malloc(sizeof(char)*(100));
        getValueOfKey(value, (char *)data, symbolKey); 
        if(value==NULL) return;
        char fileNameString[strlen(value)];
        strcpy(fileNameString,value); 
        fileNameString[strlen(fileNameString)-1] = '\0';    

        // writing what date we got them:
        time_t rawtime;
        struct tm * timeinfo;
        char timeBuffer[80];
        time ( &rawtime );
        timeinfo = localtime ( &rawtime );
        strftime(timeBuffer,80,"%x - %I:%M%p", timeinfo); 

        writeTextToFile(fileNameString, timeBuffer);      
        findValueOfKeyAndWriteToFile(fileNameString, data,  "\"p\":", "Last price: ");
        findValueOfKeyAndWriteToFile(fileNameString, data,  "\"c\":", "Trace conditions: ");
        findValueOfKeyAndWriteToFile(fileNameString, data, "\"t\":", "Timestamp:");
        findValueOfKeyAndWriteToFile(fileNameString, data, "\"v\":", "Volume: "); 
 
        clock_t end = clock();
        // calculate elapsed time by finding difference (end - receivedData) and
        // dividing the difference by CLOCKS_PER_SEC to convert to seconds
        double time_spent = (double)(end - receivedData) / CLOCKS_PER_SEC; 
	char content[80]; 
      	sprintf(content, "delay: %fs", time_spent);   
	writeTextToFile("delays_for_writing_stock_info", content);
        char content2[80]; 
      	sprintf(content2, "%f,", time_spent);
        writeTextToFile("sum_of_delays_for_writing_stock_info", content2);  

        // we add to our dequeue that we received a stock for earliest minute  
        char * valueOfStock  =(char *)malloc(sizeof(char)*(100));
        getValueOfKey(valueOfStock, (char *)data, "\"p\":");  
        double valueOfStockDouble = strtod(valueOfStock,NULL); 

        char * valueOfVolume  =(char *)malloc(sizeof(char)*(100));
        getValueOfKey(valueOfVolume, (char *)data, "\"v\":");  
        double valueOfVolumeDouble = strtod(valueOfVolume,NULL); 
       
        if(strstr(fileNameString, "AMZN") != NULL){ 
                updateExistingFrontElementOfDequeue(stocksPerMinuteDequeueAmzn, valueOfStockDouble, &frontOfDequeueAmzn, &rearOfDequeueAmzn);   
                updateExistingFrontElementOfDequeue(volumesPerMinuteDequeueAmzn, valueOfVolumeDouble, &frontOfVolumeDequeueAmzn, &rearOfVolumeDequeueAmzn);    
        }else if(strstr(fileNameString, "IBM") != NULL){
                updateExistingFrontElementOfDequeue(stocksPerMinuteDequeueIbm, valueOfStockDouble, &frontOfDequeueIbm, &rearOfDequeueIbm);    
                updateExistingFrontElementOfDequeue(volumesPerMinuteDequeueIbm, valueOfVolumeDouble, &frontOfVolumeDequeueIbm, &rearOfVolumeDequeueIbm);    
        }else if(strstr(fileNameString, "BINANCE") != NULL){
                updateExistingFrontElementOfDequeue(stocksPerMinuteDequeueBinance, valueOfStockDouble, &frontOfDequeueBinance, &rearOfDequeueBinance);
                updateExistingFrontElementOfDequeue(volumesPerMinuteDequeueBinance, valueOfVolumeDouble, &frontOfVolumeDequeueIbm, &rearOfVolumeDequeueIbm);        
        }   

        clock_t endForMovingMeanAndVolume = clock();
        // calculate elapsed time by finding difference (end - receivedData) and
        // dividing the difference by CLOCKS_PER_SEC to convert to seconds
        time_spent = (double)(endForMovingMeanAndVolume - receivedData) / CLOCKS_PER_SEC; 
        char contentForMovingMeanAndVolume[80]; 
      	sprintf(contentForMovingMeanAndVolume, "delay: %fs", time_spent);   
        writeTextToFile("delays_for_moving_mean_and_volume", contentForMovingMeanAndVolume); 

        char content3[80]; 
      	sprintf(content3, "%f,", time_spent);    
	writeTextToFile("sum_of_delays_for_moving_mean_and_volume", content3);  

       free(value);
}  
 
void writeMovingMeansForLast15MinutesStockPricesForASymbol(char *filename, double dequeue[]){ 
        int numberOfMinutesForStocks = countElementsInDequeue(dequeue);
        double sumOfStocksForAllMinutes  = getSumOfDequeuesValues(dequeue);
        double mean = sumOfStocksForAllMinutes/numberOfMinutesForStocks;  
        char charray[200]; 
        sprintf(charray, "%2.13f", mean);
        int len2 = strlen(charray);    
        char * concatenatedString = (char *) malloc( len2 +1);  
        char content [80]; 
        sprintf(content, "moving mean for last %d minutes: ", numberOfMinutesForStocks); 
        concatenateStringWithFloatString(concatenatedString , content, charray);   
        writeTextToFile(filename, concatenatedString );    
        free(concatenatedString );
}

 void writeVolumeForLast15MinutesForASymbol(char *filename, double dequeue[]){ 
        int numberOfMinutesForStocks = countElementsInDequeue(dequeue);
        double volumesForAllMinutes = getSumOfDequeuesValues(dequeue);  
        char charray[200]; 
        sprintf(charray, "%2.13f", volumesForAllMinutes);
        int len2 = strlen(charray);    
        char * concatenatedString = (char *) malloc( len2 +1);  
        char content [80]; 
        sprintf(content, "volume for last %d minutes: ", numberOfMinutesForStocks); 
        concatenateStringWithFloatString(concatenatedString , content, charray);   
        writeTextToFile(filename, concatenatedString );    
        free(concatenatedString);
}

 // we will find the last price of the stock, add it to our queue and get the mean of the last 15min 
void writeMovingMeansAndVolumesForLast15MinutesStockPricesForAllSymbols(){
        // for IBM 
        writeMovingMeansForLast15MinutesStockPricesForASymbol("MOVING_MEANS_IBM", stocksPerMinuteDequeueIbm); 
        writeVolumeForLast15MinutesForASymbol("VOLUMES_IBM", volumesPerMinuteDequeueIbm);
        // for AMZN 
        writeMovingMeansForLast15MinutesStockPricesForASymbol("MOVING_MEANS_AMZN", stocksPerMinuteDequeueAmzn);  
        writeVolumeForLast15MinutesForASymbol("VOLUMES_AMZN", volumesPerMinuteDequeueAmzn);
        // for BINANCE  
        writeMovingMeansForLast15MinutesStockPricesForASymbol("MOVING_MEANS_BINANCE", stocksPerMinuteDequeueBinance); 
        writeVolumeForLast15MinutesForASymbol("VOLUMES_BINANCE", volumesPerMinuteDequeueBinance); 
}



void findValueOfKeyAndWriteToFile( char fileNameString[], char data[], char key[], char keyForFile[]){  
        char* valueOfKey = (char *)malloc(sizeof(char)*(100));
        getValueOfKey(valueOfKey,(char *)data, key);  
        
        if(!valueOfKey){ 
                free(valueOfKey);
                return;
        }
        writeFormattedDataToFile(fileNameString, keyForFile , valueOfKey);     
        free(valueOfKey);
}

void concatenateStringWithFloatString(char* concatenatedString, char * str1,char * str2) {  
       double numberString = strtod(str2,NULL); 
       sprintf(concatenatedString, "%s%f", str1, numberString); 
 } 

void writeFormattedDataToFile(char fileName[],  char key[], char value[]){  
        if(value==NULL || fileName==NULL){
                return;
        } 
        int len1 = strlen(key);
        int len2 = strlen(value);   
        
        char * concatenatedString = (char *) malloc(strlen(key)+ strlen(value) +1); 
        concatenateStringWithFloatString(concatenatedString, key, value);   
        writeTextToFile(fileName, concatenatedString);      
        free(concatenatedString); 
}  


void subscribeToSymbol( struct lws *wsi, char symbol[]){ 
        char* out = NULL; 
        char str[50];
        sprintf(str, "{\"type\":\"subscribe\",\"symbol\":\"%s\"}", symbol); 
        int len = strlen(str); 
        out = (char *)malloc(sizeof(char)*(LWS_SEND_BUFFER_PRE_PADDING + len + LWS_SEND_BUFFER_POST_PADDING));
        memcpy(out + LWS_SEND_BUFFER_PRE_PADDING, str, len); 
        lws_write(wsi, out+LWS_SEND_BUFFER_PRE_PADDING, len, LWS_WRITE_TEXT); 
        free(out);
}

void getValueOfKey(char* value, char data[], char keyName[] ){ 
        const char *cs;
        size_t cslen;
        cs = lws_json_simple_find(data, strlen(data), keyName, &cslen);
        if (!cs) {
                lwsl_err("%s: simple_find failed for key %s and data %s\n", __func__, keyName, data); 
                return NULL;
        }  
        char fileName[cslen+2]; 
        strncpy(fileName, cs, cslen); 
        fileName[cslen+1] = '\0';

        sprintf(value, "%s", fileName);  
}

void writeTextToFile(char fileName[], char content[]){  
        FILE * fptr; 
        int sizeOfString = strlen(content);    
        int fileNameSize = strlen(fileName);   
        fptr = fopen(fileName,"a");     
        for (int i = 0; i< sizeOfString ; i++) {   
            fputc(content[i], fptr);
        }
        fputc('\n', fptr);
        fputc('\0', fptr);
        fclose(fptr); 
}
  
static void sigint_handler(int sig)
{
	interrupted = 1;
}

void *threadproc(void *arg)
{
    while(1)
    {
        gettimeofday(&t1, NULL);
        sleep(60);
          
        // 1 minute have passed. We calculate candlestick data and the moving mean and moving volumes
        gettimeofday(&t2, NULL);
         
         // for candlestick data
        calculateCandleStickDataForMinuteAndSaveThemToFile("AMZN"); 
        calculateCandleStickDataForMinuteAndSaveThemToFile("APPL"); 
        calculateCandleStickDataForMinuteAndSaveThemToFile("IBM"); 
        calculateCandleStickDataForMinuteAndSaveThemToFile("BINANCE:BTCUSDT"); 
        calculateCandleStickDataForMinuteAndSaveThemToFile("IC MARKETS:1");  
        
        // for moving mean and volume
        writeMovingMeansAndVolumesForLast15MinutesStockPricesForAllSymbols();  
        moveDequeueToNextMinute();
        resetTimers();  
    }
    return 0;
}

int main(void)
{
    pthread_t tid;
    pthread_create(&tid, NULL, &threadproc, NULL);

    struct lws_context_creation_info info;
    initializeDequeues(); 
    signal(SIGINT, sigint_handler);
    memset(&info, 0, sizeof info);

 
    struct lws *wsi = NULL;
    struct lws_protocols protocol;

    memset(&info, 0, sizeof info);

    info.port = CONTEXT_PORT_NO_LISTEN;
    info.protocols = protocols;
    info.gid = -1;
    info.uid = -1;
    info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
    context = lws_create_context(&info);

    if (!context) {
        printf(KRED"[Main] context is NULL.\n"RESET);
        return -1;
    }
    printf(KRED"[Main] context created.\n"RESET);
    
  /* schedule the first client connection attempt to happen immediately */
	lws_sul_schedule(context, 0, &mco.sul, connect_client, 1);
        int n = 0;
	while (n >= 0) n = lws_service(context, 0);
	lws_context_destroy(context);
	lwsl_user("Completed\n");  

    return 0;
}

static void connect_client(lws_sorted_usec_list_t *sul)
{
        struct lws *wsi = NULL;  
	struct my_conn *m = lws_container_of(sul, struct my_conn, sul);
        sortedUsecListT = *sul;
	 
        struct lws_client_connect_info clientConnectionInfo;

	memset(&clientConnectionInfo, 0, sizeof(clientConnectionInfo));

	       clientConnectionInfo.context = context; 

        char inputURL[300] = "ws.finnhub.io/?token=ca5l84iad3i4sbn0eskg";
        const char *urlProtocol, *urlTempPath;
        char urlPath[300];
        if (lws_parse_uri(inputURL, &urlProtocol, &clientConnectionInfo.address,  &clientConnectionInfo.port, &urlTempPath)) printf("Couldn't parse URL\n");
              
        urlPath[0] = '/';
        strncpy(urlPath + 1, urlTempPath, sizeof(urlPath) - 2);
        urlPath[sizeof(urlPath)-1] = '\0';
        clientConnectionInfo.path = urlPath;  
        clientConnectionInfo.port = 443;
        clientConnectionInfo.host = clientConnectionInfo.address;
        clientConnectionInfo.origin = clientConnectionInfo.address;
        clientConnectionInfo.ssl_connection =  LCCSCF_USE_SSL | LCCSCF_ALLOW_SELFSIGNED | LCCSCF_SKIP_SERVER_CERT_HOSTNAME_CHECK;
        clientConnectionInfo.protocol = protocols[0].name;
        clientConnectionInfo.local_protocol_name = protocols[0].name; 
        clientConnectionInfo.pwsi = &m->wsi;
        clientConnectionInfo.retry_and_idle_policy = &retry; 
        clientConnectionInfo.ietf_version_or_minus_one = -1; 

    if (!lws_client_connect_via_info(&clientConnectionInfo))
		/*
		 * Failed... schedule a retry... we can't use the _retry_wsi()  convenience wrapper api here because no valid wsi at this point.
		 */
		if (lws_retry_sul_schedule(context, 0, sul, &retry, connect_client, &m->retry_count)) {
			lwsl_err("%s: connection attempts exhausted\n", __func__);
			interrupted = 1;
		}
}
 
