#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include <fcntl.h>
#include <unistd.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <pthread.h>
#include <wait.h>

#ifndef QSIZE
#define QSIZE 8
#endif



//***** STRUCTS *****//
struct analysis{
	char* file1;
	char* file2;
	float jsd;
	int totalWords;
	struct analysis* next;
};
struct WFD {
	char* name;
	double frequency;
	int occurences;
	struct WFD* next;
	int visited;
	pthread_mutex_t lock;

};

struct repository{
	struct WFD* head;
	char* fileName;
	struct repository* next; 
	int tokens;
	pthread_mutex_t lock;

};


struct node {  
	char* name;
	struct node* next; 
	pthread_mutex_t lock;
};


struct queue_t{
	char* data[QSIZE];
	int count;
	int head;
	int open;
	pthread_mutex_t lock;
	pthread_cond_t read_ready;
	pthread_cond_t write_ready;
};

struct targs {
	struct queue_t *Q;
	int id;
	int max;
	int wait;
};

/***** GLOBAL VARIABLES/OBJECTS *****/


struct repository* r = 0;

char* suffix="";
int cont = 0;
int dqueueSize = 0;
int fqueueSize = 0;
int activeThreads = 0;
int err = 0;
struct queue_t* directoryList;
struct queue_t* fileList;
struct analysis* aList;
pthread_mutex_t mutex;


struct queue_t* init(){
	struct queue_t* Q = malloc(sizeof(struct queue_t));
	Q->head = 0;
	Q->count = 0;
	Q->open = 1;
	pthread_mutex_init(&Q->lock, NULL);
	pthread_cond_init(&Q->read_ready, NULL);
	pthread_cond_init(&Q->write_ready, NULL);

	return Q;
}

int destroy(struct queue_t *Q){
	pthread_mutex_destroy(&Q->lock);
	pthread_cond_destroy(&Q->read_ready);
	pthread_cond_destroy(&Q->write_ready);

	return 0;
}

// add item to end of queue
// if the queue is full, block until space becomes available

int enqueue(struct queue_t *Q, char* path){
	pthread_mutex_lock(&Q->lock);
	
	while (Q->count == QSIZE && Q->open) {
		
		pthread_cond_wait(&Q->write_ready, &Q->lock);
	}
	if (!Q->open) {
		pthread_mutex_unlock(&Q->lock);
		return -1;
	}

	unsigned i = Q->head + Q->count;
	if (i >= QSIZE) i -= QSIZE;

	Q->data[i] = path;
	++Q->count;

	

	

	pthread_cond_signal(&Q->read_ready);
	pthread_mutex_unlock(&Q->lock);

	

	return 0;
}


char* dequeue(struct queue_t *Q){
	
	pthread_mutex_lock(&Q->lock);
	
	if(Q->count == 0){
		activeThreads--;
		//decrement active thread
		if(!Q->open){
			pthread_mutex_unlock(&Q->lock);
			pthread_cond_signal(&Q->read_ready);
			return NULL;
		}
		while (Q->count == 0 && Q->open) {
			pthread_cond_wait(&Q->read_ready, &Q->lock);
		}
		if (Q->count == 0) {
			pthread_mutex_unlock(&Q->lock);
			return NULL;
		}
		activeThreads++;
		//increment active thread

	}

	
	
	char* item = Q->data[Q->head];

	--Q->count;
	++Q->head;
	if (Q->head == QSIZE) Q->head = 0;

	pthread_cond_signal(&Q->write_ready);
	pthread_mutex_unlock(&Q->lock);

	return item;
}

int qclose(struct queue_t *Q){
	pthread_mutex_lock(&Q->lock);
	Q->open = 0;
	pthread_cond_broadcast(&Q->read_ready);
	pthread_cond_broadcast(&Q->write_ready);
	pthread_mutex_unlock(&Q->lock); 

	return 0;
}







/***** MEMORY ALLOCATION *****/
struct analysis* initAnalysis(){
	struct analysis*  temp = malloc(sizeof(struct analysis));
	temp->file1 = "";
	temp->file2 = "";
	temp->jsd = 2;
	temp->next = 0;
	temp->totalWords = 0;

	return temp;
}

struct repository* allocateRepository(){
	struct repository* temp = malloc(sizeof(struct repository));
	temp->next = 0;
	temp->fileName = "/";
	temp->head = 0;
	temp->tokens = 0;


	return temp;
}
struct WFD* allocateWFD(){
	struct WFD* temp = malloc(sizeof(struct WFD));
	temp->name = "";
	temp->occurences = 1;
	temp->next = 0;
	temp->visited = 0;

	return temp;
}

struct node* allocateNode(char* str){
	struct node* temp = malloc(sizeof(struct node));
	char* n = str;
	temp->name = n;
	temp->next = 0;

	return temp;
}

/***** SET OBJECT *****/

struct WFD* setHead(struct WFD* head){
	struct WFD* ptr = allocateWFD();
	ptr->name = head->name;
	ptr->occurences = head->occurences;
	ptr->next = head->next;
	return ptr;
}


struct repository* setRepository(struct repository* head){
	struct repository* ptr = allocateRepository();
	ptr->fileName = head->fileName;
	struct WFD* temp = setHead(head->head);
	free(ptr->head);
	ptr->head = temp;
	ptr->next = head->next;
	return ptr;
}

struct node* copyNode(struct node* head){
	struct node* ptr = allocateNode(head->name);
	ptr->next = head->next;
	return ptr;
}

/***** FREE *****/

void freeWFD(struct WFD* head){

	struct WFD* h = head;
	head = head->next;
	free(h);
	struct WFD* temp = head;
	while(head != 0){
		temp = head;
		head=head->next;
		free(temp->name);
		free(temp);
	}
	free(head);

}

void freeRep(struct repository* head){

	struct repository* temp = head;
	while(head!=0){
		temp = head;
		head = head->next;
		free(temp->fileName);
		freeWFD(temp->head);
		free(temp);
	}

}


void freeNodes(struct node* head){

	struct node* temp;
	while(head != 0){
		temp = head;
		head=head->next;
		free(temp->name);
		free(temp);
	}
	free(head);


}
void freeStrings(struct node* head){

	struct node* temp;
	while(head != 0){
		temp = head;
		head=head->next;
		free(temp);
	}

}

/***** ENQUEUE/DEQUEUE *****/

struct repository* addRep(struct repository* head, char* fName, struct WFD* h, int t){
	struct repository* ptr = head;
	struct repository* temp = allocateRepository();
	char* hold =  calloc(strlen(fName)+1, sizeof(char));
	for(int i = 0; i< strlen(fName); i++){
		hold[i] = fName[i];
	}
	hold[strlen(hold)]='\0';
	if(head == 0){
		head = temp;
		head->fileName = hold;
		head->head = h;
		temp->tokens = t;
		return head;
	}
	while(ptr->next!= 0){
		ptr = ptr->next;
   	}
	temp->fileName = hold;
	temp->head = h;
	temp->tokens = t;
	ptr->next = temp;
	return head;
}

struct WFD* addWFD(struct WFD* head, char* str, double f){
	struct WFD* ptr = head;
	struct WFD* temp = allocateWFD();  
	char* hold = calloc(strlen(str)+1, sizeof(char)); 
	for(int i = 0; i< strlen(str); i++){
		hold[i] = str[i];
	}
	hold[strlen(hold)]='\0';
	if(head == 0){
		head = temp;
		head->name = hold;
		return head;
	}
	while(ptr->next!= 0){
		ptr = ptr->next;
	}
	temp->name = hold;
	ptr->next = temp;
	temp->frequency = f;
	return head;
}

struct analysis* addA(struct analysis* head, char* f1, char* f2, float j, int total){

	struct analysis* temp = initAnalysis();
	
	char* hold1 = calloc(strlen(f1)+1, sizeof(char)); 
	for(int i = 0; i< strlen(f1); i++){
		hold1[i] = f1[i];
	}
	hold1[strlen(hold1)]='\0'; 
	
	char* hold2 = calloc(strlen(f2)+1, sizeof(char)); 
	for(int i = 0; i< strlen(f2); i++){
		hold2[i] = f2[i];
	}
	hold2[strlen(hold2)]='\0'; 
	
	

	if(head == 0){
		head = temp;
		temp->file1 = hold1;
		temp->file2 = hold2;
		temp->jsd = j;
		temp->totalWords = total;
		return head;
	}
	struct analysis* prev = 0;
	
	struct analysis* ptr = head;

	while(ptr!= 0){
		if(total > ptr->totalWords){
			break;
		}
		prev = ptr;
		ptr = ptr->next;
	}
	
	temp->file1 = hold1;
	temp->file2 = hold2;
	temp->jsd = j;
	temp->totalWords = total;
	if(ptr == 0){
		prev->next = temp;
	}else{
		if(prev!=0){
			temp->next = prev->next;
			prev->next = temp;
		}else{
			temp->next = head;
			head = temp;
		}

	}
	
	return head;

}





char* concatFile (char* a, char* b){
	int length = strlen(a) + strlen(b);
	char* final =  calloc(length+2, sizeof(char));
	for(int i = 0; i < strlen(a); i++){
		final[i] = a[i];
	}
	final[strlen(a)] = '/';
	for(int i = 0; i < strlen(b); i++){
		final[i + strlen(a)+1] = b[i];
	}



	return final;
}




void* readFile(){
	pthread_mutex_lock(&mutex);
	if(dqueueSize<=0 && fqueueSize<=0){
		pthread_mutex_unlock(&mutex);
		return NULL;

	}
	
	

	



	char* fileName = dequeue(fileList);
	fqueueSize--;

	
	int fd = open(fileName, O_RDONLY);
	if(fd<0){ 
		perror("r1"); 
		exit(1);
	}
	char* c =  calloc(1, sizeof(char));
	char* word =  calloc(1, sizeof(char)); 
	int sz = 0;
	int tokens = 0;
	int empty = 0;
	
	struct WFD* repo = allocateWFD();
	int count = 0;
	int o = 0;
	struct WFD* temp2 = allocateWFD();
	do { //loops until end of file
		sz = read(fd,c,1);
		c[0] = toupper(c[0]);
		if(isspace(c[0]) == 0 && ((ispunct(c[0]) == 0) && (c[0] != 39))){  //not a space, and also not punctiation or apostrophe
			word[count] = c[0];
			count++;
			word =  realloc(word, (count+1)); 

		}
		if(isspace(c[0]) != 0){ //is a space... meaning word is found
			
			word[count] = '\0';
			if(word[0] != ' ' && word[0] != '\0'){
				tokens++;
			}
			struct WFD* t = setHead(repo);
			free(temp2); //need to free the temp2 linked list
			temp2 = t;
			while(temp2 !=0){

				if(strcmp(temp2->name,word)==0){ 
					temp2->occurences++;
					
					o = 1;
				}
				temp2=temp2->next;

			} 


			free(t);
			if(o == 0){

				repo = addWFD(repo, word, 0);

				empty = 1;

			}else{
				o = 0;
			}

			count = 0;
		}
		if(sz == 0){
			free(c);
			free(word);
		}
	} while(sz!=0);
	if(tokens == 0){
		free(temp2);
	}
	close(fd);
	struct WFD* bs = setHead(repo);
	

	if(empty == 0){
		tokens = 0;
	}
	r = addRep(r,fileName,bs,tokens);
	
	
	free(repo);

	pthread_mutex_unlock(&mutex);
	if(fqueueSize>0){
		readFile();
	}
	return NULL;
}


void* ForD (){ 

	pthread_mutex_lock(&mutex);
	if(dqueueSize<=0){
		pthread_mutex_unlock(&mutex);

		return NULL;
		
	}
	
	
	struct stat s;
	char* p = "";
	char* path = dequeue(directoryList); 
	dqueueSize--;

	path[strlen(path)] = '\0';


	DIR *d; 

	struct dirent *dir;

	d = opendir(path);

	while((dir = readdir(d)) != NULL){ 

		if(dir->d_name[0] == '.'){
			continue;
		}

		p = concatFile(path,dir->d_name);
		int e = stat(p, &s);

		if(e == 0){
   			 if(S_ISDIR(s.st_mode)){ 

				enqueue(directoryList,p);
				dqueueSize++;

			}else{
				char* end = &p[strlen(p)-strlen(suffix)];

				if(strcmp(end, suffix) == 0){

					enqueue(fileList, p);
					fqueueSize++;

				}
			}
		}


	}

	closedir(d);
	pthread_mutex_unlock(&mutex);
	if(dqueueSize > 0){


		ForD();
	}

	return NULL;

}



float compare(char* f1, char* f2){
	struct repository* tempR = r;
	struct WFD* w1;
	struct WFD* w2;
	double t1 =0;
	double t2 =0;
	while(tempR!=NULL){ //finding the files
		
		if(strcmp(tempR->fileName, f1) == 0){
			w1 = tempR->head->next;
			t1 = tempR->tokens;
			

		}
		if(strcmp(tempR->fileName, f2) == 0){
			w2 = tempR->head->next;
			t2 = tempR->tokens;

			
		}
		tempR = tempR->next;
	}
	if(t1 == 0 && t2 == 0){
			return 0;
	}
	
	if(t1 == 0 || t2 == 0){
		return sqrt(0.5);
	}
	
	
	int fCheck = 0; 
	int fCheck2 = 0;
	
	double kld1 = 0;
	double kld2 = 0;
	
	while(w1!=NULL){
		
		while(w2!=NULL){
			
			if(strcmp(w1->name,w2->name) == 0){
			
				double freq1 = w1->occurences/t1;
				
				double freq2 = w2->occurences/t2;

				double meanFreq = (freq1 + freq2)/2;

				kld1+=(log2(freq1/meanFreq))*freq1;

				kld2+=(log2(freq2/meanFreq))*freq2;

				w2->visited = 1; 
				fCheck = 1;
				fCheck2 = 1;
				//printf("File 1 %s %g %d %f\n", w1->name, freq1, w1->occurences, t1);
				//printf("Log1 %f\n", log2(freq1/meanFreq));
				//printf("File 2 %s %g %d %f\n", w2->name, freq2, w2->occurences, t2);
				//printf("Log2 %f\n", log2(freq2/meanFreq));
				break;
			}
			if(fCheck2 != 1){
			double freq2 = w2->occurences/t2;

			double meanFreq = (freq2)/2;

			kld2+=(log2(freq2/meanFreq))*freq2;
		//	printf("File 2 %s %g %d %f\n", w2->name, freq2, w2->occurences, t2);
		//	printf("Log2 %f\n", log2(freq2/meanFreq));

		}
		fCheck2 = 0;
			w2=w2->next;
			
		}
		

		if(fCheck != 1){
			double freq1 = w1->occurences/t1;

			double meanFreq = (freq1)/2;

			kld1+=(log2(freq1/meanFreq))*freq1;
		//	printf("File 1 %s %g %d %f\n", w1->name, freq1, w1->occurences, t1);
		//	printf("Log1 %f\n", log2(freq1/meanFreq));

		}
		fCheck = 0;
		w1=w1->next;
	}
	

	double jsd = sqrt((kld1/2) + (kld2/2));
	
	return jsd;

}

void* fileCombos(){
	struct repository* repos = r;
	struct repository* repos2;
	if(r!= NULL && r->next != NULL){
		repos2 = r->next;
	}else{
		err = 1;
		return NULL; 
	}
	double jsd = 0;

	while(repos != NULL){
		
		
		while(repos2 != NULL){
			
			pthread_mutex_lock(&mutex);
			if(strcmp(repos->fileName,repos2->fileName) != 0){
				jsd = compare(repos->fileName, repos2->fileName);

				struct analysis* ptr = aList;
				int check = 0;
				while(ptr!=0){
					if((strcmp(repos->fileName,ptr->file1) == 0 && strcmp(repos2->fileName,ptr->file2) == 0)){
						check = 1;
						break;
					}
					ptr = ptr->next;
				}
				if(check == 0){
					aList = addA(aList, repos->fileName,repos2->fileName,jsd, repos->tokens+repos2->tokens);
					
				}

					
			}
			pthread_mutex_unlock(&mutex);
			repos2 = repos2->next;
		}
		
		repos = repos->next;
		if(repos!= NULL && repos->next!= NULL){
			repos2 = repos->next;
		}
	}
	return NULL;
}

int main(int argc, char* argv[]){

	int dThread = 1;
	int fThread = 1;
	int aThread = 1;
	char* suf = ".txt";
	
	
	directoryList = init();
	fileList = init();

	for(int i = 1; i < argc; i++){
		if(argv[i][0] == '-' ){
			if(argv[i][1] == 'd'){
				char* temp = &(argv[i][2]);
				int j = 0;
				do{
					if(isdigit(temp[j]) == 0){
						perror("INVALID ARGUEMENT");
						return EXIT_FAILURE;
					}
					j++;
				}while(j < strlen(temp));
				dThread = atoi(temp);

			}else if(argv[i][1] == 'f'){
				char* temp = &(argv[i][2]);
				int j = 0;
				do{
					if(isdigit(temp[j]) == 0){
						perror("INVALID ARGUEMENT");
						return EXIT_FAILURE;
					}
					j++;
				}while(j < strlen(temp));
				fThread = atoi(temp);
			} else if(argv[i][1] == 'a'){
				char* temp = &(argv[i][2]);
				int j = 0;
				do{
					if(isdigit(temp[j]) == 0){
						perror("INVALID ARGUEMENT");
						return EXIT_FAILURE;
					}
					j++;
				}while(j < strlen(temp));
				aThread = atoi(temp);
			}else if(argv[i][1] == 's'){ 
				suf = &(argv[i][2]);


			}
			suffix = suf;

		}
		if(dThread == 0 || fThread == 0 || aThread == 0){
			return EXIT_FAILURE;
		}



	}
	struct stat u;
	int counter = 0;
	for(int i = 1; i < argc; i++){
		if(argv[i][0] != '-'){
			int e = stat(argv[i], &u);
			if(e == 0){
				if(S_ISDIR(u.st_mode)){ 

					enqueue(directoryList, argv[i]);

					dqueueSize++;
					counter++;
				}else{
					int fd = open(argv[i], O_RDONLY);
					if(fd >= 0){
						enqueue(fileList, argv[i]);
						fqueueSize++;
					} 
					close(fd);

				}
			}
		}
	}


	int totalThreads = dThread+fThread;
	pthread_t* ttid;
	pthread_mutex_init(&mutex, NULL);

	struct targs* args;
	

	
	args = malloc(totalThreads * sizeof(struct targs));
	
	ttid = malloc(totalThreads * sizeof(pthread_t));


	
	cont = counter;
	int i;

	

	for(i = 0; i < dThread; i++){
		activeThreads++;
		

		pthread_create(&ttid[i], NULL, ForD, NULL);

	}


	for(i = dThread; i < totalThreads; i++){
		
		pthread_create(&ttid[i], NULL, readFile, NULL);

	}

	for(int i=0; i<totalThreads; i++){
		pthread_join(ttid[i], NULL);

	}

	aList = initAnalysis();
	
	
	
	pthread_t* atid;
	atid = malloc(aThread*sizeof(pthread_t));

	for(i = 0; i < aThread; i++){

		pthread_create(&atid[i], NULL, fileCombos , NULL);

	}
	for(int i=0; i<aThread; i++){
		pthread_join(atid[i], NULL);

	}




	
	
	printf("\n");
	struct analysis* holder = aList;
	if(aList->jsd == 2){
		aList = aList->next;
		while(aList!=0){
			
			printf("%g %s %s \n\n",aList->jsd, aList->file1, aList->file2);
			aList = aList->next;
		}
		
		aList = holder;
		//aList=aList->next;
		//free(holder);
		while(aList!=0){
			holder = aList;
			aList = aList->next;
			free(holder->file1);
			free(holder->file2);
			free(holder);
		}
		free(aList);
	} else {
		while(aList->next!=0){
			
			printf("%g %s %s \n\n",aList->jsd, aList->file1, aList->file2);
			aList = aList->next;
		}
		
		aList = holder;
		//aList=aList->next;
		//free(holder);
		while(aList->next!=0){
			holder = aList;
			aList = aList->next;
			free(holder->file1);
			free(holder->file2);
			free(holder);
		}
		free(aList);
	}


	
	pthread_mutex_destroy(&mutex);
	destroy(directoryList);
	free(args);
	//destroy(&Q);
	//free(Q);
	destroy(fileList);
	free(ttid);
	free(atid);
	if(err == 1){
		perror("EXIT FAILURE");
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
	
}
