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

Dllist global_list;
int count = 0;
pthread_cond_t condVar = PTHREAD_COND_INITIALIZER;

void initialize_simulation(Elevator_Simulation *es)
{
  // printf("Calling initialize_simulation\n");
	global_list = new_dllist();
	es->v = global_list;
  // es->v = (void *) new_dllist();
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
  // printf("calling wait for elevator\n");
  //lock all elevators
  pthread_mutex_lock(p->es->lock);
  //add person to global list
  global_list = (Dllist) p->es->v;
  dll_append(global_list, new_jval_v((void *) p));
  pthread_mutex_unlock(p->es->lock);

  //blocking the person's condition variable
  pthread_mutex_lock(p->lock);
  pthread_cond_wait(p->cond, p->lock);
  pthread_mutex_unlock(p->lock);
  return;
}

/*: Unblock the elevator’s condition variable and block on
the person’s condition variable

blocks until elevator has moved to the person's destination floor and opened the door

*/
void wait_to_get_off_elevator(Person *p)
{
  // printf("calling wait to get off elevator \n");
  //unblock elevator by signaling to let person off elevator
  pthread_mutex_lock(p->lock);
  //wakes up elevator to let person off.
  pthread_cond_signal(p->e->cond);
  //block person person until the elevator is ready to let person off.
  pthread_cond_wait(p->cond, p->lock);
  pthread_mutex_unlock(p->lock);
  return;
}

/* Unblock the elevator’s condition variable

perform any final activties on the person

*/
void person_done(Person *p)
{
  // printf("Calling person_done\n");
  //unblock the person's elevator 
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

  // printf("calling elevator \n");
  Elevator *e = (Elevator *)arg;
  Person *p = NULL;
  while(1)
  {
    //if the there is people waiting on the global list queue
    if(!dll_empty(global_list)){
      pthread_mutex_lock(e->es->lock);
      //take the first person of global_list
      Dllist first_person = dll_first(global_list);
      p = (Person*) jval_v(dll_val(first_person));
      // //remove person from global list
      dll_delete_node(first_person);
      pthread_mutex_unlock(e->es->lock);

      // printf("person floor %s %d\n", p->fname, p->from);
      // printf("elevator’s current floor %d\n", e->onfloor);

      //if the elevator is not on the person's current floor
      //move to that floor
      if(p->from != e->onfloor)
      {
        //if the door is open close it
        if(e->door_open)
        {
          // printf("person's in elevator %s\n", p->fname);
          // printf("line 139, closing door not on the floor person is from\n");
          close_door(e);
        }
        // printf("elevator is on %s floor",p->fname);
        //move to the person's floor
        move_to_floor(e, p->from);
      }
      
      //elevator door is closed then elevator must be on the same floor
      //open the door and add the elevator to the person's e field
      if(!e->door_open)
      {
        open_door(e);
        p->e = e; // add elevator to person's e field        
      }

      // printf("elevator is on %s ",e->door_open);
      //This point the elevator should wake up the person to let them in the elevator
      pthread_mutex_lock(e->lock);
      //wakes up the person
      pthread_cond_signal(p->cond);
      pthread_mutex_unlock(e->lock);

      //blocks elevator condition until person wakes it up to get in.
      pthread_mutex_lock(e->lock);
      pthread_cond_wait(e->cond, e->lock);
      pthread_mutex_unlock(e->lock);

      // printf("elevator is on %s floor",p->fname);
      //close the door move the elevator to person's destination, and open the door
      close_door(e);
      move_to_floor(e, p->to);
      open_door(e);

      //block the elevator while signal the person to wake up to get off the elevator.
      pthread_mutex_lock(e->lock);
      pthread_cond_signal(p->cond);
      pthread_mutex_unlock(e->lock);

      //block the elevator so that elevator won't perform any action until person gets off.
      pthread_mutex_lock(e->lock);
      pthread_cond_wait(e->cond, e->lock);
      pthread_mutex_unlock(e->lock);



    }

  }
  return NULL;
}
