// Copyright (c) 2025 Kirill Gavrilov

#ifdef _WIN32
  // should be included before other headers to avoid missing definitions
  #include <windows.h>
#endif

#include "OcctQWidgetViewer.h"

#include "OcctQtTools.h"

#include <Standard_WarningsDisable.hxx>
#include <QApplication>
#include <QMessageBox>
#include <QMouseEvent>
#include <Standard_WarningsRestore.hxx>

#include <AIS_Shape.hxx>
#include <AIS_ViewCube.hxx>
#include <Aspect_DisplayConnection.hxx>
#include <Aspect_NeutralWindow.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <Message.hxx>
#include <OpenGl_GraphicDriver.hxx>

// ================================================================
// Function : OcctQWidgetViewer
// ================================================================
OcctQWidgetViewer::OcctQWidgetViewer(QWidget* theParent)
    : QWidget(theParent)
{
  Handle(Aspect_DisplayConnection) aDisp   = new Aspect_DisplayConnection();
  Handle(OpenGl_GraphicDriver)     aDriver = new OpenGl_GraphicDriver(aDisp, false);

  // create viewer
  myViewer = new V3d_Viewer(aDriver);
  myViewer->SetDefaultBackgroundColor(Quantity_NOC_BLACK);
  myViewer->SetDefaultLights();
  myViewer->SetLightOn();
  myViewer->ActivateGrid(Aspect_GT_Rectangular, Aspect_GDM_Lines);

  // create AIS context
  myContext = new AIS_InteractiveContext(myViewer);

  myViewCube = new AIS_ViewCube();
  myViewCube->SetViewAnimation(myViewAnimation);
  myViewCube->SetFixedAnimationLoop(false);
  myViewCube->SetAutoStartAnimation(true);
  myViewCube->TransformPersistence()->SetOffset2d(Graphic3d_Vec2i(100, 150));

  // note - window will be created later within initializeGL() callback!
  myView = myViewer->CreateView();
  myView->SetImmediateUpdate(false);
#ifndef __APPLE__
  myView->ChangeRenderingParams().NbMsaaSamples = 4; // warning - affects performance
#endif
  myView->ChangeRenderingParams().ToShowStats    = true;
  myView->ChangeRenderingParams().CollectedStats = (Graphic3d_RenderingParams::PerfCounters)(
    Graphic3d_RenderingParams::PerfCounters_FrameRate | Graphic3d_RenderingParams::PerfCounters_Triangles);

  // Qt widget setup
  setAttribute(Qt::WA_PaintOnScreen);
  setAttribute(Qt::WA_NoSystemBackground);
  setAttribute(Qt::WA_NativeWindow);   // request native window for this widget to create OpenGL context
  setMouseTracking(true);
  setBackgroundRole(QPalette::NoRole); // or NoBackground
  setFocusPolicy(Qt::StrongFocus);     // set focus policy to threat QContextMenuEvent from keyboard
  setUpdatesEnabled(true);

  initializeGL();
}

// ================================================================
// Function : ~OcctQWidgetViewer
// ================================================================
OcctQWidgetViewer::~OcctQWidgetViewer()
{
  Handle(Aspect_DisplayConnection) aDisp = myViewer->Driver()->GetDisplayConnection();

  // release OCCT viewer
  myContext->RemoveAll(false);
  myContext.Nullify();
  myView->Remove();
  myView.Nullify();
  myViewer.Nullify();

  aDisp.Nullify();
}

// ================================================================
// Function : dumpGlInfo
// ================================================================
void OcctQWidgetViewer::dumpGlInfo(bool theIsBasic, bool theToPrint)
{
  TColStd_IndexedDataMapOfStringString aGlCapsDict;
  myView->DiagnosticInformation(aGlCapsDict,
                                theIsBasic ? Graphic3d_DiagnosticInfo_Basic : Graphic3d_DiagnosticInfo_Complete);
  TCollection_AsciiString anInfo;
  for (TColStd_IndexedDataMapOfStringString::Iterator aValueIter(aGlCapsDict); aValueIter.More(); aValueIter.Next())
  {
    if (!aValueIter.Value().IsEmpty())
    {
      if (!anInfo.IsEmpty())
        anInfo += "\n";

      anInfo += aValueIter.Key() + ": " + aValueIter.Value();
    }
  }

  if (theToPrint)
    Message::SendInfo(anInfo);

  myGlInfo = QString::fromUtf8(anInfo.ToCString());
}

// ================================================================
// Function : initializeGL
// ================================================================
void OcctQWidgetViewer::initializeGL()
{
  const QRect           aRect = rect();
  const Graphic3d_Vec2i aViewSize(aRect.right() - aRect.left(), aRect.bottom() - aRect.top());
  const Aspect_Drawable aNativeWin = (Aspect_Drawable)winId();

  Handle(Aspect_NeutralWindow) aWindow = Handle(Aspect_NeutralWindow)::DownCast(myView->Window());
  if (!aWindow.IsNull())
  {
    aWindow->SetNativeHandle(aNativeWin);
    aWindow->SetSize(aViewSize.x(), aViewSize.y());
    myView->SetWindow(aWindow);
    dumpGlInfo(true, true);
  }
  else
  {
    aWindow = new Aspect_NeutralWindow();
    aWindow->SetVirtual(true);
    aWindow->SetNativeHandle(aNativeWin);
    aWindow->SetSize(aViewSize.x(), aViewSize.y());
    myView->SetWindow(aWindow);
    dumpGlInfo(true, true);

    myContext->Display(myViewCube, 0, 0, false);
  }

  {
    // dummy shape for testing
    TopoDS_Shape      aBox   = BRepPrimAPI_MakeBox(100.0, 50.0, 90.0).Shape();
    Handle(AIS_Shape) aShape = new AIS_Shape(aBox);
    myContext->Display(aShape, AIS_Shaded, 0, false);
  }
}

// ================================================================
// Function : closeEvent
// ================================================================
void OcctQWidgetViewer::closeEvent(QCloseEvent* theEvent)
{
  theEvent->accept();
}

// ================================================================
// Function : keyPressEvent
// ================================================================
void OcctQWidgetViewer::keyPressEvent(QKeyEvent* theEvent)
{
  if (myView.IsNull())
    return;

  const Aspect_VKey aKey = OcctQtTools::qtKey2VKey(theEvent->key());
  switch (aKey)
  {
    case Aspect_VKey_Escape: {
      QApplication::exit();
      return;
    }
    case Aspect_VKey_F: {
      myView->FitAll(0.01, false);
      update();
      return;
    }
  }
  QWidget::keyPressEvent(theEvent);
}

// ================================================================
// Function : mousePressEvent
// ================================================================
void OcctQWidgetViewer::mousePressEvent(QMouseEvent* theEvent)
{
  QWidget::mousePressEvent(theEvent);
  if (myView.IsNull())
    return;

  const Graphic3d_Vec2i  aPnt(theEvent->pos().x(), theEvent->pos().y());
  const Aspect_VKeyFlags aFlags = OcctQtTools::qtMouseModifiers2VKeys(theEvent->modifiers());
  if (UpdateMouseButtons(aPnt, OcctQtTools::qtMouseButtons2VKeys(theEvent->buttons()), aFlags, false))
    updateView();
}

// ================================================================
// Function : mouseReleaseEvent
// ================================================================
void OcctQWidgetViewer::mouseReleaseEvent(QMouseEvent* theEvent)
{
  QWidget::mouseReleaseEvent(theEvent);
  if (myView.IsNull())
    return;

  const Graphic3d_Vec2i  aPnt(theEvent->pos().x(), theEvent->pos().y());
  const Aspect_VKeyFlags aFlags = OcctQtTools::qtMouseModifiers2VKeys(theEvent->modifiers());
  if (UpdateMouseButtons(aPnt, OcctQtTools::qtMouseButtons2VKeys(theEvent->buttons()), aFlags, false))
    updateView();
}

// ================================================================
// Function : mouseMoveEvent
// ================================================================
void OcctQWidgetViewer::mouseMoveEvent(QMouseEvent* theEvent)
{
  QWidget::mouseMoveEvent(theEvent);
  if (myView.IsNull())
    return;

  const Graphic3d_Vec2i aNewPos(theEvent->pos().x(), theEvent->pos().y());
  if (UpdateMousePosition(aNewPos,
                          OcctQtTools::qtMouseButtons2VKeys(theEvent->buttons()),
                          OcctQtTools::qtMouseModifiers2VKeys(theEvent->modifiers()),
                          false))
  {
    updateView();
  }
}

// ==============================================================================
// function : wheelEvent
// ==============================================================================
void OcctQWidgetViewer::wheelEvent(QWheelEvent* theEvent)
{
  QWidget::wheelEvent(theEvent);
  if (myView.IsNull())
    return;

#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
  const Graphic3d_Vec2i aPos(Graphic3d_Vec2d(theEvent->position().x(), theEvent->position().y()));
#else
  const Graphic3d_Vec2i aPos(theEvent->pos().x(), theEvent->pos().y());
#endif
  if (!myView->Subviews().IsEmpty())
  {
    Handle(V3d_View) aPickedView = myView->PickSubview(aPos);
    if (!aPickedView.IsNull() && aPickedView != myFocusView)
    {
      // switch input focus to another subview
      OnSubviewChanged(myContext, myFocusView, aPickedView);
      updateView();
      return;
    }
  }

  if (UpdateZoom(Aspect_ScrollDelta(aPos, double(theEvent->angleDelta().y()) / 8.0)))
    updateView();
}

// =======================================================================
// function : updateView
// =======================================================================
void OcctQWidgetViewer::updateView()
{
  update();
  // if (window() != NULL) { window()->update(); }
}

// ================================================================
// Function : paintEvent
// ================================================================
void OcctQWidgetViewer::paintEvent(QPaintEvent* theEvent)
{
  if (myView.IsNull() || myView->Window().IsNull())
    return;

  Aspect_Drawable aNativeWin = (Aspect_Drawable)winId();
  if (myView->Window()->NativeHandle() != aNativeWin)
  {
    // workaround window recreation done by Qt on monitor (QScreen) disconnection
    Message::SendWarning() << "Native window handle has changed by QWidget!";
    initializeGL();
    return;
  }

  const QRect     aRect = rect();
  Graphic3d_Vec2i aViewSizeNew(aRect.right() - aRect.left(), aRect.bottom() - aRect.top());
  Graphic3d_Vec2i aViewSizeOld;

  Handle(Aspect_NeutralWindow) aWindow = Handle(Aspect_NeutralWindow)::DownCast(myView->Window());
  aWindow->Size(aViewSizeOld.x(), aViewSizeOld.y());
  if (aViewSizeNew != aViewSizeOld)
  {
    aWindow->SetSize(aViewSizeNew.x(), aViewSizeNew.y());
    myView->MustBeResized();
    myView->Invalidate();
    dumpGlInfo(true, false);

    for (const Handle(V3d_View)& aSubviewIter : myView->Subviews())
    {
      aSubviewIter->MustBeResized();
      aSubviewIter->Invalidate();
    }
  }

  // flush pending input events and redraw the viewer
  Handle(V3d_View) aView = !myFocusView.IsNull() ? myFocusView : myView;
  aView->InvalidateImmediate();
  FlushViewEvents(myContext, aView, true);
}

// ================================================================
// Function : resizeEvent
// ================================================================
void OcctQWidgetViewer::resizeEvent(QResizeEvent* theEvent)
{
  if (!myView.IsNull())
    myView->MustBeResized();
}

// ================================================================
// Function : handleViewRedraw
// ================================================================
void OcctQWidgetViewer::handleViewRedraw(const Handle(AIS_InteractiveContext)& theCtx, const Handle(V3d_View)& theView)
{
  AIS_ViewController::handleViewRedraw(theCtx, theView);
  if (myToAskNextFrame)
    updateView(); // ask more frames for animation
}

// ================================================================
// Function : OnSubviewChanged
// ================================================================
void OcctQWidgetViewer::OnSubviewChanged(const Handle(AIS_InteractiveContext)&,
                                         const Handle(V3d_View)&,
                                         const Handle(V3d_View)& theNewView)
{
  myFocusView = theNewView;
}
