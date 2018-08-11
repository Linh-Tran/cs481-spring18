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
#include <stdlib.h>
//Set up lists
Dllist global_list; //waiting_list

void initialize_simulation(Elevator_Simulation *es)
{
  global_list = new_dllist();
  es->v = global_list;
  return;
}

void initialize_elevator(Elevator *e)
{
  e->v = malloc(sizeof(int));
  *((int*)(e->v)) = 1; //set direction to up by default.
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
  pthread_mutex_lock(p->lock);
  pthread_cond_signal(p->e->cond);
  pthread_mutex_unlock(p->lock); 
  return;
}

Dllist check_for_people_to_unload(Elevator *e)
{
  Dllist unload_list = new_dllist(); //unload_list
  pthread_mutex_lock(e->es->lock);
  Dllist item;
  dll_traverse(item,e->people)
  {
    Person *p = (Person*) jval_v(dll_val(item));
    if(p->to == e->onfloor)
    { 
      dll_append(unload_list, new_jval_v((void *)p));
    }
  }
  pthread_mutex_unlock(e->es->lock);
  return unload_list;
}

Dllist check_for_people_to_load(Elevator *e)
{ 
  Dllist load_list = new_dllist();
  pthread_mutex_unlock(e->es->lock);
  Dllist item;
  global_list = (Dllist) e->es->v;
  dll_traverse(item, global_list){
    Person *p = (Person*) jval_v(dll_val(item));
    //printf("\t Should I get %s going from %d to %d?", p->fname, p->from, p->to);
    if(p->from == e->onfloor)
    {
      if((p->to > e->onfloor && *(int*)(e->v) == 1) || (p->to < e->onfloor && e->v == 0))
      {
        //printf("yes!\n");
        //set item to point next item on list.
        item = dll_prev(item);
        //delete the node that that was once where the item was
        dll_delete_node(dll_next(item));
        //append item to load_list
        dll_append(load_list, new_jval_v((void *)p));
      } 
    }
  }
  pthread_mutex_unlock(e->es->lock);
  return  load_list;
}

void move(Elevator *e)
{
  if(e->door_open)
  {
    close_door(e);
  }
  if(*((int*)(e->v)) == 1){
    move_to_floor(e, e->onfloor + 1);
    //printf("\tElevator[%d] on floor %d going up:\n", e->id, e->onfloor);
  }
  else{
    move_to_floor(e, e->onfloor - 1);
    //printf("\tElevator[%d] on floor %d going down:\n", e->id, e->onfloor);
  }
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
  Elevator *e = (Elevator *)arg;
  
  while(1)
  {
    //need to reset direction if the elevator is down going down or up
    if(e->onfloor == 1 && *((int*)(e->v)) == 0)
    {
      *((int*)(e->v)) = 1;
    }
    if((e->onfloor == e->es->nfloors) && *((int*)e->v) == 1)
    {
      *((int*)(e->v)) = 0;
    }
    
    //check for people to unload
    Dllist unload_list = check_for_people_to_unload(e);

    //unload people
    //traverse through unload_list and unload a person from the load.
    Dllist item;
    dll_traverse(item, unload_list){
      Person *p = (Person*) jval_v(dll_val(item));
      if(person == NULL)
      {
        continue;
      }

      if(!e->door_open)
      {
        open_door(e);
      }
      //wait for the person to get off.
      //block the elevator while signal the person to wake up to get off the elevator.
      pthread_mutex_lock(e->lock);
      pthread_cond_signal(p->cond);
      // pthread_mutex_unlock(e->lock);

      //block the elevator so that elevator won't perform any action until person gets off.
      // pthread_mutex_lock(e->lock);
      pthread_cond_wait(e->cond, e->lock);
      pthread_mutex_unlock(e->lock);
    }
   
    Dllist load_list = check_for_people_to_load(e);
    //load people
    Dllist ite;
    dll_traverse(ite, load_list){
      Person *p = (Person*) jval_v(dll_val(ite));
      if(person == NULL){
        continue;
      }
      if(!e->door_open)
      {
        open_door(e);
      }
      //add elevator to person's e field
      p->e = e;

      //wake person up to enter elevator
      pthread_mutex_lock(e->lock);
      //wakes up the person
      pthread_cond_signal(p->cond);
      // pthread_mutex_unlock(e->lock);

      //blocks elevator condition until person wakes it up to get in.
      // pthread_mutex_lock(e->lock);
      pthread_cond_wait(e->cond, e->lock);
      pthread_mutex_unlock(e->lock);
    }
    move(e);
  }
  return NULL;
}
