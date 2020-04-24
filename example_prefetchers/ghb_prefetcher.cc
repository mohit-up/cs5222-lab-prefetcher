#include <stdio.h>
#include <stdlib.h>
#include <queue>
#include <limits.h>
#include "../inc/prefetcher.h"

using namespace std;

#define INDEX_TABLE_SIZE 16
#define GHB_SIZE 256
#define PREFETCH_WIDTH 4

int num_prefetches = 0;
int num_no_prefetches = 0;

typedef struct
{
    unsigned long long int addr;
    int prev_ghb_entry;
}ghb_entry;

typedef struct 
{
	ghb_entry* ghb_table;
	int front;
	int rear;
	int max_size;
	int size;
}ghb_queue;

ghb_queue* createqueue(int max_elements)
{
	ghb_queue *q;
	q = (ghb_queue*) malloc(sizeof(ghb_queue));
	q->ghb_table = (ghb_entry*) malloc(sizeof(ghb_entry) * max_elements);
	q->size = 0;
	q->max_size = max_elements;
	q->front = 0;
	q->rear = -1;

	return q;
}

void dequeue(ghb_queue *q)
{
	//if queue is empty
	if (q->size == 0)
	{
		//printf("empty queue\n");
		return;
	}

	else
	{
		q->size--;
		q->front++;

		if (q->front == q-> max_size)
		{
			q->front = 0;
		}
	}
	return;
}

ghb_entry front(ghb_queue *q)
{
	if (q->size == 0)
	{
		//printf("Empty queue\n");
		exit(0);
	}

	return q->ghb_table[q->front];
}

void print_queue(ghb_queue *q)
{
	if (q->size == 0)
	{
		//printf("Empty queue\n");
		exit(0);
	}

	for (int i = q->front ; i <= q->rear ; i++)
	{
		if (i == q->rear)
		{
			//printf("%llu", q->ghb_table[i].addr);
		}

		else
		{
			//printf("%llu --> ", q->ghb_table[i].addr);	
		}
	}
	//printf("\n");
}

void enqueue(ghb_queue *q, ghb_entry entry)
{
	if (q->size  == q->max_size)
	{
		//printf("queue is full\n");
	}

	else
	{
		q->size++;
		q->rear++;

		if (q->rear == q->max_size)
		{
			q->rear = 0;
		}
		q->ghb_table[q->rear] = entry;
	}
}

typedef struct 
{
    unsigned long long int addr;
    int ghb_ptr;
	unsigned long long int lru_cycle;
} index_table_entry;

typedef struct
{
	index_table_entry index_table[INDEX_TABLE_SIZE];
	int size;
} index_table_t;

//declare index table
index_table_t index_t;

//declare ghb queue
ghb_queue *Queue = createqueue(GHB_SIZE);

void l2_prefetcher_initialize(int cpu_num)
{
  printf("Prefetching using GHB G/AC\n");
  // you can inspect these knob values from your code to see which configuration you're runnig in
  printf("Knobs visible from prefetcher: %d %d %d\n", knob_scramble_loads, knob_small_llc, knob_low_bandwidth);

  //index table initialisation

	index_t.size = 0;
    for(int i = 0 ; i < INDEX_TABLE_SIZE ; i++){
      index_t.index_table[i].addr = 0;
      index_t.index_table[i].ghb_ptr = -1;
      index_t.index_table[i].lru_cycle = ULLONG_MAX;
    }	

}

void l2_prefetcher_operate(int cpu_num, unsigned long long int addr, unsigned long long int ip, int cache_hit)
{
  // uncomment this line to see all the information available to make prefetch decisions
  // printf("(0x%llx 0x%llx %d %d %d) \n", addr, ip, cache_hit, get_l2_read_queue_occupancy(0), get_l2_mshr_occupancy(0));

  int tracker = -1;

  if (!cache_hit)
  {
    if (Queue->size >= GHB_SIZE)
    {
      int removed = Queue->front;
      dequeue(Queue);
      
      for (int i = 0 ; i < index_t.size ; i++)
      {
        if (index_t.index_table[i].addr == addr)
        {
          tracker = i;
        }

        if (index_t.index_table[i].ghb_ptr == removed)
        {
          index_t.index_table[i].ghb_ptr = -1;
        }
      }
	  }

    ghb_entry *entry = new ghb_entry;
    entry->addr = addr;

    if (tracker == -1)
    {
      entry->prev_ghb_entry = -1;
    }

    else
    {
      entry->prev_ghb_entry = index_t.index_table[tracker].ghb_ptr;
      //printf("Some history there\n");
    }

	  enqueue(Queue, *entry);

    if (tracker == -1)
    {
        // Index table is full, one entry need to be evicted
        if (index_t.size == INDEX_TABLE_SIZE)
        {
          int evicted_index;
          unsigned long long int lowest_lru_cycle = ULLONG_MAX;

          for (int i = 0 ; i < INDEX_TABLE_SIZE ; i++)
          {
            if (lowest_lru_cycle > index_t.index_table[i].lru_cycle)
            {
              evicted_index = i;
              lowest_lru_cycle = index_t.index_table[i].lru_cycle;
            }
          }

          index_t.index_table[evicted_index].lru_cycle = get_current_cycle(0);
          index_t.index_table[evicted_index].addr = addr;
          index_t.index_table[evicted_index].ghb_ptr = Queue->rear;
          tracker = evicted_index;
        }

        else
        {
          index_t.index_table[index_t.size].lru_cycle = get_current_cycle(0);
          index_t.index_table[index_t.size].addr = addr;
          index_t.index_table[index_t.size].ghb_ptr = Queue->rear;
          tracker = index_t.size;
          index_t.size++;
        }
    }

    else
    {
      index_t.index_table[tracker].lru_cycle = get_current_cycle(0);
      index_t.index_table[tracker].ghb_ptr = Queue->rear; 
      //printf("found index table change its pointer to %d\n", Queue->rear);
    }

    int index = Queue->ghb_table[Queue->rear].prev_ghb_entry;

    // check MSHR occupancy to decide whether to prefetch into the L2 or LLC
    for (int i = 0 ; i < PREFETCH_WIDTH ; i++)
    {
      if (index == -1)
      {
        break;
      }

      //printf("Prefetch width: %d\n", i);

      int access = index + 1;
      if (access == Queue->max_size)
        access = 0;

      unsigned long long int pf_address = Queue->ghb_table[access].addr;
      //printf("prefetch addr: %llu\n", pf_address);

      if (((pf_address>>12) != (addr>>12)) && (get_l2_mshr_occupancy(0) < 8))
      {
        l2_prefetch_line(0, addr, pf_address, FILL_L2);
        num_prefetches++;
      }

      else
        num_no_prefetches++;
    } 

  }
}

void l2_cache_fill(int cpu_num, unsigned long long int addr, int set, int way, int prefetch, unsigned long long int evicted_addr)
{
  // uncomment this line to see the information available to you when there is a cache fill event
  //printf("0x%llx %d %d %d 0x%llx\n", addr, set, way, prefetch, evicted_addr);
}

void l2_prefetcher_heartbeat_stats(int cpu_num)
{
  printf("Prefetcher heartbeat stats\n");
}

void l2_prefetcher_warmup_stats(int cpu_num)
{
  printf("Prefetcher warmup complete stats\n\n");
}

void l2_prefetcher_final_stats(int cpu_num)
{
  printf("Prefetcher final stats\n");
}