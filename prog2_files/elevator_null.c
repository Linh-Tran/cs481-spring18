/* elevator_null.c
   Null solution for the elevator threads lab.
   Jim Plank
   CS560
   Lab 2
   January, 2009
 */

#include <stdio.h>
#include <pthread.h>
#include "elevator.h"
#include "dllist.h"
/*set up the global list and a condition
variable for blocking elevators.*/

Dllist *global_list;
pthread_cond_t condVar = PTHREAD_COND_INITIALIZER;

void initialize_simulation(Elevator_Simulation *es)
{
  printf("Calling initialize_simulation\n");
	global_list = new_dllist();
	es->v = global_list;
	return;
}

void initialize_elevator(Elevator *e)
{
  return;
}

void initialize_person(Person *e)
{
  return;
}

/*append the person to the global list. Signal the condition
variable for blocking elevators. Block on the person’s condition variable. blocks until an elevator is on the person’s floor with its door open. At
this time, the person’s e field will be set to the appropriate elevator.

blocks until an elevator is on the person's floor with its door open

*/
void wait_for_elevator(Person *p)
{

  printf("calling wait for elevator\n");

  //lock the critical section
  pthread_mutex_lock(p->es->lock);
  dll_append(global_list, new_jval_v((void *) p));
  pthread_mutex_unlock(p->es->lock);

  //blocking the person's condition variable
  pthread_cond_wait(p->cond, p->lock);

  return;
}

/*: Unblock the elevator’s condition variable and block on
the person’s condition variable

blocks until elevator has moved to the person's destination floor and opened the door

*/
void wait_to_get_off_elevator(Person *p)
{
  printf("calling wait to get off elevator \n");
  pthread_mutex_lock(p->lock);
  pthread_cond_signal(p->e->cond);
  pthread_mutex_unlock(p->lock);

  return;
}

/* Unblock the elevator’s condition variable

perform any final activties on the person

*/
void person_done(Person *p)
{
  printf("Calling person_done\n");
  pthread_mutex_lock(p->lock);
  pthread_cond_signal(p->e->cond);
  pthread_mutex_unlock(p->lock); 
  return;
}

/* Each elevator is a while loop. Check the global list and if it’s empty,
block on the condition variable for blocking elevators. When the elevator gets a person to service, it
moves to the appropriate floor and opens its door. It puts itself into the person’s e field, then signals the
person and blocks until the person wakes it up. When it wakes up, it goes to the person’s destination
floor, opens its door, signals the person and blocks. When the person wakes it up, it closes its door
and re-executes its while loop.*/
void *elevator(void *arg)
{

  printf("calling elevator \n");
  Elevator *e = (Elevator *)arg;
  while(!dll_empty(global_list))
  {
    pthread_mutex_lock(e->lock);
    pthread_cond_wait(e->cond, e->lock);
    pthread_mutex_unlock(e->lock);

    
    printf("Size of list %lu", sizeof(struct dllist));
    // if(!(dllist_empty(e->people)))
    // {
    // 	printf("Number of people in elevator list %lu", sizeof(struct dllist));
    // }
  }
  return NULL;
}