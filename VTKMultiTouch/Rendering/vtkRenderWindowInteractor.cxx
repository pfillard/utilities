/*=========================================================================

  Program:   Visualization Toolkit
  Module:    $RCSfile: vtkRenderWindowInteractor.cxx,v $

  Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
  All rights reserved.
  See Copyright.txt or http://www.kitware.com/Copyright.htm for details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  See the above copyright notice for more information.

=========================================================================*/
#include "vtkRenderWindowInteractor.h"

#include "vtkCamera.h"
#include "vtkCommand.h"
#include "vtkGraphicsFactory.h"
#include "vtkInteractorStyleSwitch.h"
#include "vtkMath.h"
#include "vtkPropPicker.h"
#include "vtkRenderWindow.h"
#include "vtkRenderer.h"
#include "vtkRendererCollection.h"
#include "vtkDebugLeaks.h"
#include "vtkObserverMediator.h"
#include <vtkstd/map>

// PIMPL'd class to keep track of timers. It maps the ids returned by CreateTimer()
// to the platform-specific representation for timer ids.
struct vtkTimerStruct
{
  int Id;
  int Type;
  unsigned long Duration;
  vtkTimerStruct() : Id(0),Type(vtkRenderWindowInteractor::OneShotTimer),Duration(10) {}
  vtkTimerStruct(int platformTimerId, int timerType, unsigned long duration)
    {
      this->Id = platformTimerId;
      this->Type = timerType;
      this->Duration = duration;
    }
};


class vtkTimerIdMap : public vtkstd::map<int,vtkTimerStruct> {};
typedef vtkstd::map<int,vtkTimerStruct>::iterator vtkTimerIdMapIterator;

// Initialize static variable that keeps track of timer ids for 
// render window interactors.
static int vtkTimerId = 1; 

//----------------------------------------------------------------------------
// Needed when we don't use the vtkStandardNewMacro.
vtkInstantiatorNewMacro(vtkRenderWindowInteractor);
//----------------------------------------------------------------------------

vtkCxxSetObjectMacro(vtkRenderWindowInteractor,Picker,vtkAbstractPicker);

//----------------------------------------------------------------------
// Construct object so that light follows camera motion.
vtkRenderWindowInteractor::vtkRenderWindowInteractor()
{
  this->RenderWindow    = NULL;
  this->InteractorStyle = NULL;
  this->SetInteractorStyle(vtkInteractorStyleSwitch::New()); 
  this->InteractorStyle->Delete();
  
  this->LightFollowCamera = 1;
  this->Initialized = 0;
  this->Enabled = 0;
  this->EnableRender = true;
  this->DesiredUpdateRate = 15;
  // default limit is 3 hours per frame
  this->StillUpdateRate = 0.0001;
  
  this->Picker = this->CreateDefaultPicker();
  this->Picker->Register(this);
  this->Picker->Delete();

  this->EventPosition[0] = this->LastEventPosition[0] = 0;
  this->EventPosition[1] = this->LastEventPosition[1] = 0;

  this->EventSize[0] = 0;
  this->EventSize[1] = 0;

  this->Size[0] = 0;
  this->Size[1] = 0;
  
  this->NumberOfFlyFrames = 15;
  this->Dolly = 0.30;
  
  this->AltKey = 0;
  this->ControlKey = 0;
  this->ShiftKey = 0;
  this->KeyCode = 0;
  this->RepeatCount = 0;
  this->KeySym = 0;
  this->TimerEventId = 0;
  this->TimerEventType = 0;
  this->TimerEventDuration = 0;
  this->TimerEventPlatformId = 0;
  this->PinchGestureFactor = 1.0;
  this->RotateGestureAngle = 0.0;
  this->SwipeGestureDirection = 0;

  this->TimerMap = new vtkTimerIdMap;
  this->TimerDuration = 10;
  this->ObserverMediator = 0;
  this->HandleEventLoop = false;
  
  this->UseTDx=false; // 3DConnexion device.
}

//----------------------------------------------------------------------
vtkRenderWindowInteractor::~vtkRenderWindowInteractor()
{
  if (this->InteractorStyle != NULL)
    {
    this->InteractorStyle->UnRegister(this);
    }
  if ( this->Picker)
    {
    this->Picker->UnRegister(this);
    }
  if ( this->KeySym )
    {
    delete [] this->KeySym;
    }
  if ( this->ObserverMediator)
    {
    this->ObserverMediator->Delete();
    }
  delete this->TimerMap;
  
  this->SetRenderWindow(0);
}

//----------------------------------------------------------------------
vtkRenderWindowInteractor *vtkRenderWindowInteractor::New()
{
  // First try to create the object from the vtkObjectFactory
  vtkObject* ret = 
    vtkGraphicsFactory::CreateInstance("vtkRenderWindowInteractor");
  if ( ret )
    {
    return static_cast<vtkRenderWindowInteractor *>(ret);
    }
#ifdef VTK_DEBUG_LEAKS
  vtkDebugLeaks::ConstructClass("vtkRenderWindowInteractor");
#endif
  return new vtkRenderWindowInteractor;
}

//----------------------------------------------------------------------
void vtkRenderWindowInteractor::Render()
{
  if (this->RenderWindow && this->Enabled && this->EnableRender)
    {
    this->RenderWindow->Render();
    }
  // outside the above test so that third-party code can redirect
  // the render to the appropriate class
  this->InvokeEvent(vtkCommand::RenderEvent, NULL);
}

//----------------------------------------------------------------------
// treat renderWindow and interactor as one object.
// it might be easier if the GetReference count method were redefined.
void vtkRenderWindowInteractor::UnRegister(vtkObjectBase *o)
{
  if (this->RenderWindow && this->RenderWindow->GetInteractor() == this &&
      this->RenderWindow != o)
    {
    if (this->GetReferenceCount()+this->RenderWindow->GetReferenceCount() == 3)
      {
      this->RenderWindow->SetInteractor(NULL);
      this->SetRenderWindow(NULL);
      }
    }

  this->vtkObject::UnRegister(o);
}

//----------------------------------------------------------------------
void vtkRenderWindowInteractor::SetRenderWindow(vtkRenderWindow *aren)
{
  if (this->RenderWindow != aren)
    {
    // to avoid destructor recursion
    vtkRenderWindow *temp = this->RenderWindow;
    this->RenderWindow = aren;
    if (temp != NULL)
      {
      temp->UnRegister(this);
      }
    if (this->RenderWindow != NULL)
      {
      this->RenderWindow->Register(this);
      if (this->RenderWindow->GetInteractor() != this)
        {
        this->RenderWindow->SetInteractor(this);
        }
      }
    }
}

//----------------------------------------------------------------------
void vtkRenderWindowInteractor::SetInteractorStyle(vtkInteractorObserver *style)
{
  if (this->InteractorStyle != style)
    {
    // to avoid destructor recursion
    vtkInteractorObserver *temp = this->InteractorStyle;
    this->InteractorStyle = style;
    if (temp != NULL)
      {
      temp->SetInteractor(0);
      temp->UnRegister(this);
      }
    if (this->InteractorStyle != NULL)
      {
      this->InteractorStyle->Register(this);
      if (this->InteractorStyle->GetInteractor() != this)
        {
        this->InteractorStyle->SetInteractor(this);
        }
      }
    }
}

//----------------------------------------------------------------------
void vtkRenderWindowInteractor::UpdateSize(int x,int y) 
{
  // if the size changed send this on to the RenderWindow
  if ((x != this->Size[0])||(y != this->Size[1]))
    {
    this->Size[0] = this->EventSize[0] = x;
    this->Size[1] = this->EventSize[1] = y;
    this->RenderWindow->SetSize(x,y);
    }
}

//----------------------------------------------------------------------
// Creates an instance of vtkPropPicker by default
vtkAbstractPropPicker *vtkRenderWindowInteractor::CreateDefaultPicker()
{
  return vtkPropPicker::New();
}

//----------------------------------------------------------------------
void vtkRenderWindowInteractor::ExitCallback()
{
  if (this->HasObserver(vtkCommand::ExitEvent))
    {
    this->InvokeEvent(vtkCommand::ExitEvent,NULL);
    }
  else
    {
    this->TerminateApp();
    }
}

//----------------------------------------------------------------------
void vtkRenderWindowInteractor::UserCallback()
{
  this->InvokeEvent(vtkCommand::UserEvent,NULL);
}

//----------------------------------------------------------------------
void vtkRenderWindowInteractor::StartPickCallback()
{
  this->InvokeEvent(vtkCommand::StartPickEvent,NULL);
}

//----------------------------------------------------------------------
void vtkRenderWindowInteractor::EndPickCallback()
{
  this->InvokeEvent(vtkCommand::EndPickEvent,NULL);
}

//----------------------------------------------------------------------
void vtkRenderWindowInteractor::FlyTo(vtkRenderer *ren, double x, double y, double z)
{
  double flyFrom[3], flyTo[3];
  double d[3], focalPt[3];
  int i, j;

  flyTo[0]=x; flyTo[1]=y; flyTo[2]=z;
  ren->GetActiveCamera()->GetFocalPoint(flyFrom);
  for (i=0; i<3; i++)
    {
    d[i] = flyTo[i] - flyFrom[i];
    }
  double distance = vtkMath::Normalize(d);
  double delta = distance/this->NumberOfFlyFrames;
  
  for (i=1; i<=NumberOfFlyFrames; i++)
    {
    for (j=0; j<3; j++)
      {
      focalPt[j] = flyFrom[j] + d[j]*i*delta;
      }
    ren->GetActiveCamera()->SetFocalPoint(focalPt);
    ren->GetActiveCamera()->Dolly(this->Dolly/this->NumberOfFlyFrames + 1.0);
    ren->GetActiveCamera()->OrthogonalizeViewUp();
    ren->ResetCameraClippingRange();
    this->Render();
    }
}

//----------------------------------------------------------------------
void vtkRenderWindowInteractor::FlyToImage(vtkRenderer *ren, double x, double y)
{
  double flyFrom[3], flyTo[3];
  double d[3], focalPt[3], position[3], positionFrom[3];
  int i, j;

  flyTo[0]=x; flyTo[1]=y;
  ren->GetActiveCamera()->GetFocalPoint(flyFrom);  flyTo[2] = flyFrom[2];
  ren->GetActiveCamera()->GetPosition(positionFrom);
  for (i=0; i<2; i++)
    {
    d[i] = flyTo[i] - flyFrom[i];
    }
  d[2] = 0.0;
  double distance = vtkMath::Normalize(d);
  double delta = distance/this->NumberOfFlyFrames;
  
  for (i=1; i<=NumberOfFlyFrames; i++)
    {
    for (j=0; j<3; j++)
      {
      focalPt[j] = flyFrom[j] + d[j]*i*delta;
      position[j] = positionFrom[j] + d[j]*i*delta;
      }
    ren->GetActiveCamera()->SetFocalPoint(focalPt);
    ren->GetActiveCamera()->SetPosition(position);
    ren->GetActiveCamera()->Dolly(this->Dolly/this->NumberOfFlyFrames + 1.0);
    ren->ResetCameraClippingRange();
    this->Render();
    }
}

//----------------------------------------------------------------------------
vtkRenderer* vtkRenderWindowInteractor::FindPokedRenderer(int x,int y) 
{
  vtkRendererCollection *rc;
  vtkRenderer *aren;
  vtkRenderer *currentRenderer=NULL, *interactiveren=NULL, *viewportren=NULL;
  int numRens, i;

  rc = this->RenderWindow->GetRenderers();
  numRens = rc->GetNumberOfItems();
  
  for (i = numRens -1; (i >= 0) && !currentRenderer; i--) 
    {
    aren = static_cast<vtkRenderer *>(rc->GetItemAsObject(i));
    if (aren->IsInViewport(x,y) && aren->GetInteractive()) 
      {
      currentRenderer = aren;
      }

    if (interactiveren == NULL && aren->GetInteractive())
      {
      // Save this renderer in case we can't find one in the viewport that
      // is interactive.
      interactiveren = aren;
      }
    if (viewportren == NULL && aren->IsInViewport(x, y))
      {
      // Save this renderer in case we can't find one in the viewport that 
      // is interactive.
      viewportren = aren;
      }
    }//for all renderers
  
  // We must have a value.  If we found an interactive renderer before, that's
  // better than a non-interactive renderer.
  if ( currentRenderer == NULL )
    {
    currentRenderer = interactiveren;
    }
  
  // We must have a value.  If we found a renderer that is in the viewport,
  // that is better than any old viewport (but not as good as an interactive
  // one).
  if ( currentRenderer == NULL )
    {
    currentRenderer = viewportren;
    }

  // We must have a value - take anything.
  if ( currentRenderer == NULL) 
    {
    aren = rc->GetFirstRenderer();
    currentRenderer = aren;
    }

  return currentRenderer;
}


// Timer methods. There are two basic groups of methods, those for backward
// compatibility (group #1) and those that operate on specific timers (i.e.,
// use timer ids). The first group of methods implicitly assume that there is
// only one timer at a time running. This was okay in the old days of VTK when 
// only the interactors used timers. However with the introduction of new 3D
// widgets into VTK multiple timers often run simultaneously.
//
//old-style group #1
int vtkRenderWindowInteractor::CreateTimer(int timerType) 
{
  int platformTimerId, timerId;
  if ( timerType == VTKI_TIMER_FIRST ) 
    {
    unsigned long duration = this->TimerDuration;
    timerId = vtkTimerId; //just use current id, assume we don't have mutliple timers
    platformTimerId = this->InternalCreateTimer(timerId,RepeatingTimer,duration);
    if ( 0 == platformTimerId )
      {
      return 0;
      }
    (*this->TimerMap)[timerId] = vtkTimerStruct(platformTimerId,RepeatingTimer,duration);
    return timerId;
    }

  else //VTKI_TIMER_UPDATE is just updating last created timer
    {
    return 1; //do nothing because repeating timer has been created
    }
} 

//old-style group #1
//just destroy last one created
int vtkRenderWindowInteractor::DestroyTimer() 
{
  int timerId = vtkTimerId;
  vtkTimerIdMapIterator iter = this->TimerMap->find(timerId);
  if ( iter != this->TimerMap->end() )
    {
    this->InternalDestroyTimer((*iter).second.Id);
    this->TimerMap->erase(iter);
    return 1;
    }

  return 0;
} 

//new-style group #2 returns timer id
int vtkRenderWindowInteractor::CreateRepeatingTimer(unsigned long duration) 
{
  int timerId = ++vtkTimerId;
  int platformTimerId = this->InternalCreateTimer(timerId,RepeatingTimer,duration);
  if ( 0 == platformTimerId )
    {
    return 0;
    }
  (*this->TimerMap)[timerId] = vtkTimerStruct(platformTimerId,RepeatingTimer,duration);
  return timerId;
} 

//new-style group #2 returns timer id
int vtkRenderWindowInteractor::CreateOneShotTimer(unsigned long duration) 
{
  int timerId = ++vtkTimerId;
  int platformTimerId = this->InternalCreateTimer(timerId,OneShotTimer,duration);
  if ( 0 == platformTimerId )
    {
    return 0;
    }
  (*this->TimerMap)[timerId] = vtkTimerStruct(platformTimerId,OneShotTimer,duration);
  return timerId;
} 

//new-style group #2 returns type (non-zero unless bad timerId)
int vtkRenderWindowInteractor::IsOneShotTimer(int timerId)
{
  vtkTimerIdMapIterator iter = this->TimerMap->find(timerId);
  if ( iter != this->TimerMap->end() )
    {
    return ((*iter).second.Type == OneShotTimer);
    }
  return 0;
} 

//new-style group #2 returns duration (non-zero unless bad timerId)
unsigned long vtkRenderWindowInteractor::GetTimerDuration(int timerId)
{
  vtkTimerIdMapIterator iter = this->TimerMap->find(timerId);
  if ( iter != this->TimerMap->end() )
    {
    return (*iter).second.Duration;
    }
  return 0;
} 

//new-style group #2 returns non-zero if timer reset
int vtkRenderWindowInteractor::ResetTimer(int timerId) 
{
  vtkTimerIdMapIterator iter = this->TimerMap->find(timerId);
  if ( iter != this->TimerMap->end() )
    {
    this->InternalDestroyTimer((*iter).second.Id);
    int platformTimerId = this->InternalCreateTimer(timerId, (*iter).second.Type,
                                                    (*iter).second.Duration);
    if ( platformTimerId != 0 )
      {
      (*iter).second.Id = platformTimerId;
      return 1;
      }
    else
      {
      this->TimerMap->erase(iter);
      }
    }
  return 0;
} 

//new-style group #2 returns non-zero if timer destroyed
int vtkRenderWindowInteractor::DestroyTimer(int timerId) 
{
  vtkTimerIdMapIterator iter = this->TimerMap->find(timerId);
  if ( iter != this->TimerMap->end() )
    {
    this->InternalDestroyTimer((*iter).second.Id);
    this->TimerMap->erase(iter);
    return 1;
    }
  return 0;
} 

// Stubbed out dummys
int vtkRenderWindowInteractor::InternalCreateTimer(int vtkNotUsed(timerId), int vtkNotUsed(timerType), 
                                                   unsigned long vtkNotUsed(duration))
{
  return 0;
}

int vtkRenderWindowInteractor::InternalDestroyTimer(int vtkNotUsed(platformTimerId))
{
  return 0;
}

// Translate from platformTimerId to the corresponding (VTK) timerId.
// Returns 0 (invalid VTK timerId) if platformTimerId is not found in the map.
// This first stab at an implementation just iterates the map until it finds
// the sought platformTimerId. If performance becomes an issue (lots of timers,
// all firing frequently...) we could speed this up by making a reverse map so
// this method is a quick map lookup.
int vtkRenderWindowInteractor::GetVTKTimerId(int platformTimerId)
{
  int timerId = 0;
  vtkTimerIdMapIterator iter = this->TimerMap->begin();
  for ( ; iter != this->TimerMap->end(); ++iter )
    {
    if ((*iter).second.Id == platformTimerId)
      {
      timerId = (*iter).first;
      break;
      }
    }

  return timerId;
}

// Access to the static variable
int vtkRenderWindowInteractor::GetCurrentTimerId()
{
  return vtkTimerId;
}

//----------------------------------------------------------------------------
void vtkRenderWindowInteractor::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os,indent);

  os << indent << "InteractorStyle:    " << this->InteractorStyle << "\n";
  os << indent << "RenderWindow:    " << this->RenderWindow << "\n";
  if ( this->Picker )
    {
    os << indent << "Picker: " << this->Picker << "\n";
    }
  else
    {
    os << indent << "Picker: (none)\n";
    }
  if ( this->ObserverMediator )
    {
    os << indent << "Observer Mediator: " << this->ObserverMediator << "\n";
    }
  else
    {
    os << indent << "Observer Mediator: (none)\n";
    }
  os << indent << "LightFollowCamera: " << (this->LightFollowCamera ? "On\n" : "Off\n");
  os << indent << "DesiredUpdateRate: " << this->DesiredUpdateRate << "\n";
  os << indent << "StillUpdateRate: " << this->StillUpdateRate << "\n";
  os << indent << "Initialized: " << this->Initialized << "\n";
  os << indent << "Enabled: " << this->Enabled << "\n";
  os << indent << "EnableRender: " << this->EnableRender << "\n";
  os << indent << "EventPosition: " << "( " << this->EventPosition[0] <<
    ", " << this->EventPosition[1] << " )\n";
  os << indent << "LastEventPosition: " << "( " << this->LastEventPosition[0] 
     << ", " << this->LastEventPosition[1] << " )\n";
  os << indent << "EventSize: " << "( " << this->EventSize[0] <<
    ", " << this->EventSize[1] << " )\n";
  os << indent << "Viewport Size: " << "( " << this->Size[0] <<
    ", " << this->Size[1] << " )\n";
  os << indent << "Number of Fly Frames: " << this->NumberOfFlyFrames <<"\n";
  os << indent << "Dolly: " << this->Dolly <<"\n";
  os << indent << "ControlKey: " << this->ControlKey << "\n";
  os << indent << "AltKey: " << this->AltKey << "\n";
  os << indent << "ShiftKey: " << this->ShiftKey << "\n";
  os << indent << "KeyCode: " << this->KeyCode << "\n";
  os << indent << "KeySym: " << (this->KeySym ? this->KeySym : "(null)") 
     << "\n";
  os << indent << "RepeatCount: " << this->RepeatCount << "\n";
  os << indent << "Timer Duration: " << this->TimerDuration << "\n";
  os << indent << "TimerEventId: " << this->TimerEventId << "\n";
  os << indent << "TimerEventType: " << this->TimerEventType << "\n";
  os << indent << "TimerEventDuration: " << this->TimerEventDuration << "\n";
  os << indent << "TimerEventPlatformId: " << this->TimerEventPlatformId << "\n";
  os << indent << "UseTDx: " << this->UseTDx << endl;
}

//----------------------------------------------------------------------------
void vtkRenderWindowInteractor::Initialize() 
{
  this->Initialized=1; 
  this->Enable();
  this->Render();
}

//----------------------------------------------------------------------------
void vtkRenderWindowInteractor::HideCursor() 
{ 
  this->RenderWindow->HideCursor(); 
}

//----------------------------------------------------------------------------
void vtkRenderWindowInteractor::ShowCursor() 
{ 
  this->RenderWindow->ShowCursor(); 
}

//----------------------------------------------------------------------------
vtkObserverMediator *vtkRenderWindowInteractor::GetObserverMediator()
{
  if ( !this->ObserverMediator )
    {
    this->ObserverMediator = vtkObserverMediator::New();
    this->ObserverMediator->SetInteractor(this);
    }

  return this->ObserverMediator;
}
