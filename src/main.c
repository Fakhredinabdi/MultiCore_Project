#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <pthread.h>
#include <time.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <inttypes.h>

static unsigned long long collisions   = 0;
static unsigned long long unique_words = 0;

typedef struct MapEntry {
    const char      *key;
    unsigned int     idx;
    struct MapEntry *next;
} MapEntry;

typedef struct MapBucket {
    MapEntry       *head;
    pthread_mutex_t lock;
} MapBucket;

static MapBucket *mapBuckets = NULL;
static size_t     mapCapacity = 0;


static size_t free_probe = 0;

typedef struct {
    char        *ptr;
    size_t       length;
    unsigned int index;
} StringMetadata;

typedef struct {
    char           **entries;
    size_t           capacity;
    pthread_mutex_t *locks;
} HashTable;

typedef struct {
    size_t           start_idx, end_idx;
    StringMetadata  *metadata;
    HashTable       *table;
} ThreadArgs;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
static inline uint64_t rotl64(uint64_t x,int8_t r){
    return (x<<r)|(x>>(64-r));
}
static inline uint64_t fmix64(uint64_t k){
    k ^= k>>33; k *= 0xff51afd7ed558ccdULL;
    k ^= k>>33; k *= 0xc4ceb9fe1a85ec53ULL;
    k ^= k>>33;
    return k;
}
static void MurmurHash3_x64_128(const void *key,int len,void *out){
    const uint8_t *data=(const uint8_t*)key;
    const int nblocks=len/16;
    uint64_t h1=0,h2=0;
    const uint64_t c1=0x87c37b91114253d5ULL;
    const uint64_t c2=0x4cf5ad432745937fULL;
    const uint64_t *blocks=(const uint64_t*)data;
    for(int i=0;i<nblocks;i++){
        uint64_t k1=blocks[2*i], k2=blocks[2*i+1];
        k1*=c1; k1=rotl64(k1,31); k1*=c2; h1^=k1;
        h1=rotl64(h1,27); h1+=h2; h1=h1*5+0x52dce729;
        k2*=c2; k2=rotl64(k2,33); k2*=c1; h2^=k2;
        h2=rotl64(h2,31); h2+=h1; h2=h2*5+0x38495ab5;
    }
    const uint8_t *tail=data+nblocks*16;
    uint64_t k1=0,k2=0;
    switch(len&15){
      case 15: k2^=(uint64_t)tail[14]<<48;
      case 14: k2^=(uint64_t)tail[13]<<40;
      case 13: k2^=(uint64_t)tail[12]<<32;
      case 12: k2^=(uint64_t)tail[11]<<24;
      case 11: k2^=(uint64_t)tail[10]<<16;
      case 10: k2^=(uint64_t)tail[9] << 8;
      case  9: k2^=(uint64_t)tail[8] << 0;  k2*=c2; k2=rotl64(k2,33); k2*=c1; h2^=k2;
      case  8: k1^=(uint64_t)tail[7] <<56;
      case  7: k1^=(uint64_t)tail[6] <<48;
      case  6: k1^=(uint64_t)tail[5] <<40;
      case  5: k1^=(uint64_t)tail[4] <<32;
      case  4: k1^=(uint64_t)tail[3] <<24;
      case  3: k1^=(uint64_t)tail[2] <<16;
      case  2: k1^=(uint64_t)tail[1] << 8;
      case  1: k1^=(uint64_t)tail[0] << 0;  k1*=c1; k1=rotl64(k1,31); k1*=c2; h1^=k1;
    }
    h1^=len; h2^=len; h1+=h2; h2+=h1;
    h1=fmix64(h1); h2=fmix64(h2);
    h1+=h2; h2+=h1;
    ((uint64_t*)out)[0]=h1; ((uint64_t*)out)[1]=h2;
}
#pragma GCC diagnostic pop

static HashTable* create_hash_table(size_t capacity){
    HashTable *ht = malloc(sizeof(HashTable));
    if(!ht){ perror("malloc HT"); exit(1); }
    ht->capacity = capacity;
    ht->entries  = calloc(capacity,sizeof(char*));
    ht->locks    = malloc(capacity*sizeof(pthread_mutex_t));
    if(!ht->entries||!ht->locks){ perror("alloc HT arrays"); exit(1); }
    for(size_t i=0;i<capacity;i++)
        pthread_mutex_init(&ht->locks[i],NULL);
    return ht;
}

static void insert_with_two_choice(HashTable *table,
                                   StringMetadata *md, size_t idx)
{
    const char *str   = md[idx].ptr;
    size_t      len   = md[idx].length;
    size_t      cap   = table->capacity;

    uint64_t h128[2];
    MurmurHash3_x64_128(str,(int)len,h128);
    size_t mh = (size_t)(h128[0] % mapCapacity);

    pthread_mutex_lock(&mapBuckets[mh].lock);
    for(MapEntry *e = mapBuckets[mh].head; e; e = e->next){
        if(strcmp(e->key,str)==0){
            md[idx].index = e->idx;
            pthread_mutex_unlock(&mapBuckets[mh].lock);
            return;
        }
    }
    pthread_mutex_unlock(&mapBuckets[mh].lock);

    unsigned int h1 = (unsigned int)(h128[0] % cap);
    unsigned int h2 = (unsigned int)(h128[1] % cap);
    unsigned int slot=0;
    int did_probe = 0;

    unsigned int a = h1<h2? h1: h2;
    unsigned int b = h1<h2? h2: h1;
    pthread_mutex_lock(&table->locks[a]);
    if(a!=b) pthread_mutex_lock(&table->locks[b]);

    if(table->entries[h1] == NULL)        slot = h1;
    else if(table->entries[h2] == NULL)   slot = h2;
    else                                   did_probe = 1;

    if(!did_probe){
        table->entries[slot] = (char*)str;
        if(a!=b) pthread_mutex_unlock(&table->locks[b]);
        pthread_mutex_unlock(&table->locks[a]);
    } else {
        if(a!=b) pthread_mutex_unlock(&table->locks[b]);
        pthread_mutex_unlock(&table->locks[a]);
        while(1){
            size_t cand = __sync_fetch_and_add(&free_probe,1) % cap;
            pthread_mutex_lock(&table->locks[cand]);
            if(table->entries[cand]==NULL){
                slot = (unsigned int)cand;
                table->entries[cand] = (char*)str;
                pthread_mutex_unlock(&table->locks[cand]);
                break;
            }
            pthread_mutex_unlock(&table->locks[cand]);
        }
    }

    md[idx].index = slot;

    __sync_fetch_and_add(&unique_words,1);
    if(did_probe) __sync_fetch_and_add(&collisions,1);

    MapEntry *ne = malloc(sizeof(MapEntry));
    if(!ne){ perror("malloc MapEntry"); exit(1); }
    ne->key  = str;
    ne->idx  = slot;

    pthread_mutex_lock(&mapBuckets[mh].lock);
      ne->next        = mapBuckets[mh].head;
      mapBuckets[mh].head = ne;
    pthread_mutex_unlock(&mapBuckets[mh].lock);
}

static void* thread_func(void *v){
    ThreadArgs *t = v;
    for(size_t i = t->start_idx; i < t->end_idx; i++)
        insert_with_two_choice(t->table, t->metadata, i);
    return NULL;
}

static int create_results_dir(void){
    struct stat st={0};
    if(stat("results",&st)==-1){
      if(mkdir("results",0777)==-1){
        perror("mkdir(results)"); return 1;
      }
    }
    return 0;
}

static int write_indices(const StringMetadata *md,
                         size_t    lineCount,
                         size_t    data_size,
                         int       threads,
                         size_t    tsize,
                         double    elapsed_ms)
{
    if(mkdir("results",0755)!=0 && errno!=EEXIST){
        perror("mkdir(results)"); return 1;
    }

    char path[256];
    snprintf(path,sizeof(path),
      "results/Results_MCC_030402_99106458_%zu_%d_%zu.txt",
      data_size, threads, tsize);

    FILE *out=fopen(path,"w");
    if(!out){ perror("fopen output"); return 1; }

    fprintf(out,"ExecutionTime: %.0f ms\n",elapsed_ms);
    fprintf(out,"NumberOfHandledCollision: %llu\n", collisions);

    for(size_t i=0;i<lineCount;i++){
        fprintf(out,"%u",md[i].index);
        if(i+1<lineCount) fputc(',',out);
    }
    fputc('\n',out);
    fclose(out);
    return 0;
}

typedef struct {
    size_t data_size;
    int    threads;
    size_t tsize;
    char  *filename;
} ProgramArgs;

static size_t parse_size(const char *s){
    size_t mult=1,len=strlen(s);
    if(len>1){
      char c=s[len-1];
      if(c=='K'||c=='k'){ mult=1000; len--; }
      else if(c=='M'||c=='m'){ mult=1000000; len--; }
    }
    char buf[32];
    if(len>=sizeof(buf)) return 0;
    memcpy(buf,s,len); buf[len]=0;
    char *e; unsigned long v=strtoul(buf,&e,10);
    return (e!=buf && *e==0)?v*mult:0;
}

static int parse_args(int argc,char *argv[],ProgramArgs *A){
    if(argc!=9) goto bad;
    for(int i=1;i<argc;i+=2){
      if     (!strcmp(argv[i],"--data_size")) A->data_size=parse_size(argv[i+1]);
      else if(!strcmp(argv[i],"--threads"))   A->threads   =atoi(argv[i+1]);
      else if(!strcmp(argv[i],"--tsize"))     A->tsize     =parse_size(argv[i+1]);
      else if(!strcmp(argv[i],"--input"))     A->filename  =argv[i+1];
      else goto bad;
    }
    if(!A->data_size||A->threads<1||!A->tsize||!A->filename) goto bad;
    return 0;
  bad:
    fprintf(stderr,
      "Usage: %s --data_size <size> --threads <num>\\\n"
      "          --tsize <hash-table-size> --input <file>\\\n",
      argv[0]);
    return 1;
}

static int preprocess(const char *fn,size_t *lines,size_t *bytes){
    FILE *f=fopen(fn,"r"); if(!f){ perror("fopen"); return 1; }
    char buf[1024];
    *lines=*bytes=0;
    while(fgets(buf,sizeof(buf),f)){
      (*lines)++; *bytes+=strlen(buf);
    }
    fclose(f);
    return 0;
}

static int alloc_mem(size_t lc,size_t bs,
                     StringMetadata **MD,char **DB){
    *MD=malloc(lc*sizeof(StringMetadata));
    *DB=malloc(bs+1);
    if(!*MD||!*DB){ perror("alloc"); return 1; }
    return 0;
}

static int read_data(const char *fn,size_t lc,
                     StringMetadata *MD,char *DB){
    FILE *f=fopen(fn,"r"); if(!f){ perror("fopen"); return 1; }
    char line[1024]; size_t off=0,cnt=0;
    while(cnt<lc && fgets(line,sizeof(line),f)){
        size_t L=strlen(line);
        if(L&&line[L-1]=='\n') line[--L]=0;
        memcpy(DB+off,line,L+1);
        MD[cnt].ptr=DB+off; MD[cnt].length=L;
        off+=L+1; cnt++;
    }
    fclose(f);
    return 0;
}

static int run_app(const ProgramArgs *A){
    size_t lineCount,totalBytes;
    if(preprocess(A->filename,&lineCount,&totalBytes)) return 1;
    if(lineCount==0){ fprintf(stderr,"Empty file\n"); return 1; }

    StringMetadata *MD; char *DB;
    if(alloc_mem(lineCount,totalBytes,&MD,&DB)) return 1;
    if(read_data(A->filename,lineCount,MD,DB)){
      free(MD); free(DB); return 1;
    }

    collisions = unique_words = 0;

    HashTable *ht = create_hash_table(A->tsize);

    mapCapacity = A->tsize * 2 + 1;
    mapBuckets  = malloc(mapCapacity * sizeof(MapBucket));
    if(!mapBuckets){ perror("malloc mapBuckets"); exit(1); }
    for(size_t i=0;i<mapCapacity;i++){
        mapBuckets[i].head = NULL;
        pthread_mutex_init(&mapBuckets[i].lock, NULL);
    }

    if(create_results_dir()) return 1;

    pthread_t  *threads = malloc(A->threads * sizeof(pthread_t));
    ThreadArgs *T       = malloc(A->threads * sizeof(ThreadArgs));
    size_t chunk = lineCount / A->threads;

    struct timespec st,en;
    clock_gettime(CLOCK_MONOTONIC,&st);

    for(int t=0;t<A->threads;t++){
      T[t].start_idx = t * chunk;
      T[t].end_idx   = (t==A->threads-1) ? lineCount : (t+1)*chunk;
      T[t].metadata  = MD;
      T[t].table     = ht;
      pthread_create(&threads[t],NULL,thread_func,&T[t]);
    }
    for(int t=0;t<A->threads;t++)
      pthread_join(threads[t],NULL);

    clock_gettime(CLOCK_MONOTONIC,&en);
    double elapsed_s  = (en.tv_sec - st.tv_sec)
                      + (en.tv_nsec - st.tv_nsec)*1e-9;
    double elapsed_ms = elapsed_s * 1000.0;

    if(write_indices(MD,lineCount,
                     A->data_size,
                     A->threads,
                     A->tsize,
                     elapsed_ms)!=0)
      return 1;

    printf("Threads:%d  Time:%.3fs  Unique:%llu  CollRate:%.4f%%\n",
      A->threads, elapsed_s, unique_words,
      unique_words ? (double)collisions/unique_words*100.0 : 0.0);

    free(threads); free(T);
    for(size_t i=0;i<ht->capacity;i++)
        pthread_mutex_destroy(&ht->locks[i]);
    free(ht->locks); free(ht->entries); free(ht);

    for(size_t i=0;i<mapCapacity;i++){
        pthread_mutex_destroy(&mapBuckets[i].lock);
        MapEntry *e = mapBuckets[i].head;
        while(e){
          MapEntry *n = e->next;
          free(e);
          e = n;
        }
    }
    free(mapBuckets);

    free(MD); free(DB);
    return 0;
}

int main(int argc,char *argv[]){
    ProgramArgs A={0};
    if(parse_args(argc,argv,&A)) return 1;
    return run_app(&A);
}
