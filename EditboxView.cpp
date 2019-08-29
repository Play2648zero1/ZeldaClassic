#ifndef __GTHREAD_HIDE_WIN32API                             
#define __GTHREAD_HIDE_WIN32API 
#endif                            //prevent indirectly including windows.h

#include <assert.h>

#include "EditboxNew.h"
#include "zc_alleg.h"
#include "jwin.h"
#include <map>

extern int scheme[];

#ifndef _MSC_VER
#define max(a,b)  ((a)>(b)?(a):(b))
#define min(a,b)  ((a)<(b)?(a):(b))
#endif

void EditboxView::update()
{
  assert(model);
  layoutPage();
}

void EditboxView::initialize(EditboxModel *model)
{
  this->model = model;
  //add a "ghost newline" to the buffer if necessary

  string nl = "";
  Unicode::insertAtIndex(nl,'\n',0);
  unsigned int nlsize = nl.size();
  if(model->getBuffer().size() < nlsize || model->getBuffer().substr(model->getBuffer().size()-nlsize,nlsize) != nl)
    model->getBuffer() += nl;
  dbuf = create_bitmap_ex(8, host->w, host->h);
  init();
  update();
}

void EditboxView::draw()
{
  assert(model);
}

EditboxView::~EditboxView()
{
  destroy_bitmap(dbuf);
}

///////////////////////////////////////////////////////////////////////////////////////

void EditboxView::lineUp()
{
  CursorPos cp = model->findCursor();
  list<LineData>::reverse_iterator it = list<LineData>::reverse_iterator(cp.it);
  if(it != model->getLines().rend())
  {
    int startindex = model->getCursor().getPosition()-it->numchars-cp.index;
    int index = Unicode::getIndexOfWidth(it->line, model->getCursor().getPreferredX(), textfont);
    model->getCursor().updateCursor(startindex+index);
  }
}

void EditboxView::lineDown()
{
  CursorPos cp = model->findCursor();
  list<LineData>::iterator it = cp.it;
  it++;
  int startindex = model->getCursor().getPosition()+cp.it->numchars-cp.index;
  if(it != model->getLines().end())
  {
    int index = Unicode::getIndexOfWidth(it->line, model->getCursor().getPreferredX(), textfont);
    model->getCursor().updateCursor(startindex+index);
  }
}

void EditboxView::lineHome()
{
  CursorPos cp = model->findCursor();
  int newindex = model->getCursor().getPosition()-cp.index;
  model->getCursor().updateCursor(newindex);
  model->getCursor().setPreferredX();
}

void EditboxView::lineEnd()
{
  CursorPos cp = model->findCursor();
  int newindex = model->getCursor().getPosition()-cp.index+cp.it->numchars-1;
  model->getCursor().updateCursor(newindex);
  model->getCursor().setPreferredX();
}

void EditboxView::pageDown()
{
  int textheight = text_height(textfont);
  int height = getAreaHeight();
  int numlines = height/textheight;
  for(int i=0; i<int(numlines);i++)
  {
    lineDown();
  }
}

void EditboxView::pageUp()
{
  int textheight = text_height(textfont);
  int height = getAreaHeight();
  int numlines = height/textheight;
  for(int i=0; i<numlines;i++)
  {
    lineUp();
  }
}

void EditboxView::invertRectangle(int x1, int y1, int x2, int y2)
{
  //I don't feel like dicking around with colormaps right now
  //this method is SLOOOW, big efficiency opportunity here
  static std::map<int, int> invmap;
  PALETTE pal;
  get_palette(pal);
  RGB color;
  //don't wast time drawing in stupid places
  x1 = max(x1, 0);
  x1 = min(x1, host->w);
  x2 = max(x2,0);
  x2 = min(x2, host->w);
  y1 = max(y1, 0);
  y1 = min(y1, host->h);
  y2 = max(y2, 0);
  y2 = min(y2, host->h);
  for(int i=x1; i<x2; i++)
  {
    for(int j=y1; j<y2; j++)
    {
      int c = getpixel(dbuf, i,j);
      if(c != -1)
      {
        std::map<int, int>::iterator it = invmap.find(c);
        int invcolor;
        if(it == invmap.end())
        {
          get_color(c, &color);
          unsigned char r = ~color.r;
          unsigned char g = ~color.g;
          unsigned char b = ~color.b;
          invcolor = bestfit_color(pal,r,g,b);
          invmap[c] = invcolor;
        }
        else
          invcolor = it->second;
        putpixel(dbuf, i,j,invcolor);
      }
    }
  }
}

////////////////////////////////////////////////////////////////////////////////////////

void BasicEditboxView::init()
{
  area_xstart = host->x+2;
  area_ystart = host->y;
  area_width = max(0, host->w-18); //scrollbar
  area_height = host->h;
  view_width = area_width;
  
  //make the initial lines
  model->makeLines(model->getLines(), model->getBuffer());
}

void BasicEditboxView::ensureCursorOnScreen()
{
  CursorPos cp = model->findCursor();
  int textheight = text_height(textfont);
  int cystart = cp.lineno*textheight;
  int cyend	= cystart+textheight;
  view_y = min(view_y, cystart);
  view_y = max(view_y, cyend-area_height);
  view_x = min(view_x, cp.x);
  view_x = max(view_x, cp.x-area_width);
  //enforce hard limits
  enforceHardLimits();
}

void BasicEditboxView::enforceHardLimits()
{
  int textheight = text_height(textfont);
  int ymost = max(area_height, (int)model->getLines().size()*textheight);
  view_y = max(view_y, 0);
  view_y = min(view_y, ymost-area_height);
  int xmost = max(area_width, view_width);
  view_x = max(view_x, 0);
  view_x = min(view_x, xmost-area_width);
}

BasicEditboxView::~BasicEditboxView()
{
  for(list<LineData>::iterator it = model->getLines().begin(); it != model->getLines().end(); it++)
  {
    destroy_bitmap(it->strip);
  }
  model->getLines().clear();
}

CharPos BasicEditboxView::findCharacter(int x, int y)
{
  int absolutey = y-area_ystart+view_y;
  int textheight = text_height(textfont);
  int lineno = absolutey/textheight;
  lineno = max(lineno, 0);
  lineno = min(lineno, (int)(model->getLines().size())-1);
  int totalindex = 0;
  //NOTE: future optimization opportunity
  list<LineData>::iterator it = model->getLines().begin();
  for(int i=0; i<lineno; i++)
  {
    totalindex += it->numchars;
    it++;
  }
  CharPos rval;
  rval.it = it;
  rval.lineIndex = Unicode::getIndexOfWidth(it->line, -area_xstart+x+view_x, textfont);
  rval.totalIndex = totalindex + rval.lineIndex;
  return rval;
}

void BasicEditboxView::scrollDown()
{
  int textheight = text_height(textfont);
  view_y += textheight;
  enforceHardLimits();
}

void BasicEditboxView::scrollUp()
{
  int textheight = text_height(textfont);
  view_y -= textheight;
  enforceHardLimits();
}

void BasicEditboxView::scrollLeft()
{
  view_x -= 4;
  enforceHardLimits();
}

void BasicEditboxView::scrollRight()
{
  view_x += 4;
  enforceHardLimits();
}

void BasicEditboxView::draw()
{
  rectfill(dbuf, 0, 0, host->w, host->h, bgcolor);
  set_clip_rect(dbuf, area_xstart-host->x, area_xstart-host->y, area_xstart-host->x+area_width, area_ystart-host->y+area_height);
  
  int textheight = text_height(textfont);
  int y = -view_y;
  for(list<LineData>::iterator it = model->getLines().begin(); it != model->getLines().end(); it++)
  {
    if(y >= area_ystart-host->y && y <= area_ystart+host->y + area_height)
    blit((*it).strip, dbuf, 0, 0, area_xstart-host->x-view_x, area_ystart-host->y+y, view_width, textheight);
    y+=textheight;
  }
  set_clip_rect(dbuf, 0,0,host->w,host->h);
  //draw cursor
  if(model->getCursor().isVisible())
  {
    CursorPos cp = model->findCursor();
    int textheight = text_height(textfont);
    int cursory = cp.lineno*text_height(textfont);
  //GAH, too many damn coordinate offsets :-/
    vline(dbuf, area_xstart-host->x+cp.x-view_x-1, area_ystart-host->y+cursory-view_y-1, area_ystart-host->y+cursory-view_y+textheight, fgcolor);
  }
  //draw selection
  if(model->getSelection().hasSelection())
  {
    pair<int, int> selection = model->getSelection().getSelection();
    CursorPos selstart = model->findIndex(selection.first);
    CursorPos selend = model->findIndex(selection.second);
    if(selstart.lineno == selend.lineno)
    {
      //invert the selection rectangle
      int starty = area_ystart-host->y-view_y+selstart.lineno*textheight;
      int startx = area_xstart-host->x+selstart.x-view_x;
      int endx = area_xstart-host->x+selend.x-view_x;
      invertRectangle(startx, starty, endx, starty+textheight);
    }
    else
    {
      //do the starting line
      int starty = area_ystart-host->y-view_y + selstart.lineno*textheight;
      int startx = area_xstart-host->x+selstart.x-view_x;
      invertRectangle(startx,starty,area_width,starty+textheight);
      //do intermediate lines
      for(int line = selstart.lineno+1; line < selend.lineno; line++)
      {
        invertRectangle(area_xstart-host->x, area_ystart-host->y-view_y+line*textheight, area_xstart-host->x+area_width, area_ystart-host->y-view_y+(line+1)*textheight);
      }
      //do the last line
      int endx = area_xstart-host->x+selend.x-view_x;
      invertRectangle(area_xstart-host->x,area_ystart-host->y-view_y+selend.lineno*textheight, endx, area_ystart-host->y-view_y+(selend.lineno+1)*textheight);
    }
  }
  drawExtraComponents();
  vsync();
  blit(dbuf, screen, 0, 0, host->x, host->y,host->w, host->h);
  set_clip_rect(screen, 0, 0,SCREEN_W,SCREEN_H);
}

bool BasicEditboxView::mouseClick(int x, int y)
{
  //set the cursor
  CharPos cp = findCharacter(x,y);
    model->getCursor().updateCursor(cp.totalIndex);
    model->getCursor().setPreferredX();
    model->getSelection().restartSelection(model->getCursor());
    return true;
}

bool BasicEditboxView::mouseDrag(int x, int y)
{
  int textheight = text_height(textfont);
  if(model->getSelection().isSelecting())
  {
    pair<int, int> oldsel = model->getSelection().getSelection();
    CharPos cp = findCharacter(x,y);
    model->getCursor().updateCursor(cp.totalIndex);
    model->getSelection().adjustSelection(model->getCursor());
    if(y < area_ystart)
    {
      view_y = max(0, view_y-1);
    }
    if(y > area_ystart+area_height)
    {
      int ymost = max(area_height, (int)model->getLines().size()*textheight);
      view_y = min(ymost-area_height, view_y+1);
    }
    if(x < area_xstart)
      view_x = max(0, view_x-1);
    if(x > area_xstart+area_width)
    {
      int xmost = max(area_width, view_width);
      view_x = min(xmost-area_width, view_x+1);
    }
    if(oldsel != model->getSelection().getSelection())
      return true;
  }
  return false;
}

bool BasicEditboxView::mouseRelease(int x, int y)
{
  model->getSelection().doneSelection();
  return false;
}

void BasicEditboxView::createStripBitmap(list<LineData>::iterator it, int width)
{
   //now create the bitmap
    int textheight = text_height(textfont);
  if(it->strip)
    destroy_bitmap(it->strip);
  it->strip = create_bitmap_ex(8,width,textheight);
  rectfill(it->strip, 0,0,width, textheight, bgcolor);
    Unicode::textout_ex_nonstupid(it->strip, textfont, (*it).line, 0, 0, fgcolor, bgcolor);
}

////////////////////////////////////////////////////////////////////////////////////////

void EditboxVScrollView::init()
{
  BasicEditboxView::init();
  toparrow_x = host->x+host->w-16;
  toparrow_y = host->y;
  toparrow_state=0;
  bottomarrow_x = host->x+host->w-16;
  bottomarrow_y = host->y+host->h-16;
  bottomarrow_state = 0;
  barstate = 0;
}

void EditboxVScrollView::drawExtraComponents()
{
  int textheight = text_height(textfont);
  //draw the scrollbar
  draw_arrow_button(dbuf, toparrow_x-host->x, toparrow_y-host->y, 16, 16, true, toparrow_state);
  draw_arrow_button(dbuf, bottomarrow_x-host->x, bottomarrow_y-host->y, 16, 16, false, bottomarrow_state);
  if(!sbarpattern)
  {
      sbarpattern = create_bitmap_ex(bitmap_color_depth(screen),2,2);
      putpixel(sbarpattern, 0, 1, scheme[jcLIGHT]);
      putpixel(sbarpattern, 1, 0, scheme[jcLIGHT]);
      putpixel(sbarpattern, 0, 0, scheme[jcBOX]);
      putpixel(sbarpattern, 1, 1, scheme[jcBOX]);
  }

  drawing_mode(DRAW_MODE_COPY_PATTERN, sbarpattern, 0, 0);
  int barstart = toparrow_y + 16 - host->y;
  int barend = bottomarrow_y - host->y-1;
  if(barstart < barend)
  rectfill(dbuf, toparrow_x-host->x, barstart, toparrow_x-host->x+15, barend, 0);
  solid_mode();
  //compute the bar button, based on view_y
  int totallen = model->getLines().size()*textheight;
  int available = bottomarrow_y-(toparrow_y+16);
  if(available < 0)
  {
    baroff=barlen=0;
  }
 
  else
  {
    //view_y:totallen = baroff:available
    baroff = (available*view_y)/totallen;
    //area_height:totallen = barlen:available
    barlen = (available*area_height)/max(totallen,area_height)+1;
    //clip to reasonable values
    barlen = max(barlen, 2);
    baroff = min(baroff, available-barlen);
  }
  if(barlen > 0)
  {
    jwin_draw_button(dbuf, toparrow_x-host->x, toparrow_y+16-host->y+baroff, 16, barlen, barstate, 1);
  }
}

EditboxVScrollView::~EditboxVScrollView()
{
  destroy_bitmap(sbarpattern);
}

bool EditboxVScrollView::mouseClick(int x, int y)
{
  //check if in text area
  if(area_ystart <= y && y <= area_ystart+area_height)
  {
    if(area_xstart <= x && x <= area_xstart+area_width)
    {
      return BasicEditboxView::mouseClick(x,y);
    }
    if(toparrow_x <= x && x <= toparrow_x + 16)
    {
      // clicked on an arrow, or the slider
      if(barstate == 1)
      {
        //adjust
        int deltay = barstarty-y;
        barstarty = y;
        //deltay:available = dealtaview_y:totallen
        int available = bottomarrow_y-(toparrow_y+16);
        int textheight = text_height(textfont);
        int totallen = model->getLines().size()*textheight;
        view_y -= (totallen*deltay)/available;
        enforceHardLimits();
        return true;
      }
      if(toparrow_y <= y && y <= toparrow_y + 16)
      {
        //clicked on top arrow
        scrollUp();
        toparrow_state = 1;
        return true;
      }
      if(bottomarrow_y <= y && y <= bottomarrow_y + 16)
      {
        scrollDown();
        bottomarrow_state = 1;
        return true;
      }
      else
      {
        //clicked the slider
        if(toparrow_y+16+baroff <= y && y <= toparrow_y+16+baroff+barlen)
        {
          //clicked the bar itself
          barstarty = y;
          barstate = 1;
          return true;
        }
        else
        {
          //"teleport"
          //adjust click by half of length of slider
          y -= toparrow_y+16+barlen/2;
          int available = bottomarrow_y-(toparrow_y+16);
          int textheight = text_height(textfont);
          int totallen = model->getLines().size()*textheight;
          //y:available= view_y:totallen
          view_y = (y*totallen)/available;
          enforceHardLimits();
          return true;
        }
      }
    }
  }
  return mouseClickOther(x,y);
}

bool EditboxVScrollView::mouseDrag(int x, int  y)
{
  int textheight;
  textheight = text_height(textfont);
  if(model->getSelection().isSelecting())
  {
    return BasicEditboxView::mouseDrag(x,y);
  }
  else
  {
    //maybe pressing arrow, or sliding?
    if(toparrow_state == 1)
    {
      scrollUp();
      return true;
    }
    if(bottomarrow_state == 1)
    {
      scrollDown();
      return true;
    }
    if(barstate == 1)
    {
      //fake a click
      //first, clip the coords
      int fakex = toparrow_x+1;
      return mouseClick(fakex,y);
    }
    return mouseDragOther(x,y);
  }
  return false;
}

bool EditboxVScrollView::mouseRelease(int x, int y)
{
  BasicEditboxView::mouseRelease(x,y);
  toparrow_state = 0;
  bottomarrow_state = 0;
  barstate = 0;
  return false;
}

//////////////////////////////////////////////////////////////////////////////////////
void EditboxWordWrapView::layoutPage()
{
  //check all lines
  for(list<LineData>::iterator it = model->getLines().begin(); it != model->getLines().end(); it++)
  {
    if(!it->dirtyflag)
      continue;
    int numchars = (*it).numchars;
    string &s = (*it).line;
    //accumulate up until the maximum line width
    int totalwidth=0;
    int i;
    for(i=0; i<numchars; i++)
    {
      if(i==0)
      {
        //we must be more accepting of the first character/word since the box
        //might not be wide enough.
        int c = Unicode::getCharAtIndex(s,0);
        if(c == ' ' || c == '\t' || c == '\n')
        {
          //whitespace
          //accept and continue
          totalwidth += Unicode::getCharWidth(c, textfont);
          continue;
        }
        else
        {
          //word
          pair<int, int> offandwidth = Unicode::munchWord(s,i,textfont);
          totalwidth += offandwidth.second;
          i += offandwidth.first-1;
          continue;
        }
      }
      else
      {
        int c = Unicode::getCharAtIndex(s,i);
        if(c == ' ' || c == '\t' || c == '\n')
        {
          //whitespace
          totalwidth += Unicode::getCharWidth(c, textfont);
          if(totalwidth > area_width)
            break;
        }
        else
        {
          pair<int, int> offandwidth = Unicode::munchWord(s,i,textfont);
          totalwidth += offandwidth.second;
          if(totalwidth > area_width)
            break;
          i += offandwidth.first-1;
        }
      }
    }
    if(i < numchars)
    {
      //we have wrapped early.
      string newline = s.substr(i,s.size()-i);
      LineData newdata = {newline, numchars-i, true, true,NULL};
      list<LineData>::iterator it2 = it;
      it2++;
      model->getLines().insert(it2,newdata);            
      (*it).line = s.substr(0,i);
      (*it).numchars  = i;
      (*it).newlineterminated = false;
    }
  createStripBitmap(it, area_width);
  (*it).dirtyflag = false;
  }
}

//////////////////////////////////////////////////////////////////////////////////////

void EditboxNoWrapView::layoutPage()
{
  view_width = area_width;
  for(list<LineData>::iterator it = model->getLines().begin(); it != model->getLines().end(); it++)
  {
    int length = Unicode::getLength(it->line);
    int totalwidth=0;
    //efficiency opportunity
    for(int i=0; i<length; i++)
    {
      totalwidth += Unicode::getCharWidth(Unicode::getCharAtIndex(it->line,i),textfont);
    }
    if(totalwidth > view_width)
      view_width=totalwidth;
    if(!it->dirtyflag)
      continue;
    createStripBitmap(it, totalwidth);
    (*it).dirtyflag=false;
  }
}

void EditboxNoWrapView::init()
{
  EditboxVScrollView::init();
  bottomarrow_y -= 16;
  area_height -=16;
  leftarrow_x = host->x;
  leftarrow_y = host->y+host->h-16;
  leftarrow_state=0;
  rightarrow_x = host->x+host->w-32;
  rightarrow_y = host->y+host->h-16;
  rightarrow_state = 0;
  hbarstate = 0;
}

void EditboxNoWrapView::drawExtraComponents()
{
  EditboxVScrollView::drawExtraComponents();
  int textheight;
  textheight = text_height(textfont);
  //draw the scrollbar
  draw_arrow_button_horiz(dbuf, leftarrow_x-host->x, leftarrow_y-host->y, 16, 16, true, leftarrow_state);
  draw_arrow_button_horiz(dbuf, rightarrow_x-host->x, rightarrow_y-host->y, 16, 16, false, rightarrow_state);
    drawing_mode(DRAW_MODE_COPY_PATTERN, sbarpattern, 0, 0);
  int hbarstart = leftarrow_x + 16 - host->x;
  int hbarend = rightarrow_x - host->x-1;
  if(hbarstart < hbarend)
  rectfill(dbuf, hbarstart, leftarrow_y-host->y, hbarend, leftarrow_y-host->y+15, 0);
  solid_mode();
  //compute the bar button, based on view_y
  int totallen = view_width;
  int available = rightarrow_x-(leftarrow_x+16);
  if(available < 0)
  {
    hbaroff=hbarlen=0;
  }
 
  else
  {
    //view_x:totallen = baroff:available
    hbaroff = (available*view_x)/totallen;
    //area_width:totallen = barlen:available
    hbarlen = (available*area_width)/max(totallen,area_width)+1;
    //clip to reasonable values
    hbarlen = max(hbarlen, 2);
    hbaroff = min(hbaroff, available-hbarlen);
  }
  if(hbarlen > 0)
  {
    jwin_draw_button(dbuf, leftarrow_x-host->x+hbaroff+16, leftarrow_y-host->y, hbarlen, 16, hbarstate, 1);
  }
}

bool EditboxNoWrapView::mouseRelease(int x, int y)
{
  leftarrow_state = 0;
  rightarrow_state = 0;
  hbarstate = 0;
  return EditboxVScrollView::mouseRelease(x,y);
}

bool EditboxNoWrapView::mouseDragOther(int x, int y)
{
  //maybe pressing arrow, or sliding?
  if(leftarrow_state == 1)
    {
      scrollLeft();
      return true;
    }
  if(rightarrow_state == 1)
    {
      scrollRight();
      return true;
    }
    if(hbarstate == 1)
    {
      //fake a click
      //first, clip the coords
      int fakey = leftarrow_y+1;
      return mouseClick(x,fakey);
    }
    return false;
}

bool EditboxNoWrapView::mouseClickOther(int x, int y)
{
  if(leftarrow_x <= x && x <= rightarrow_x+16)
  {
    if(leftarrow_y <= y && y <= leftarrow_y+16)
    {
      // clicked on an arrow, or the slider
      if(hbarstate == 1)
      {
        //adjust
        int deltax = hbarstartx-x;
        hbarstartx = x;
        //deltax:available = dealtaview_x:totallen
        int available = leftarrow_x-(rightarrow_x+16);
        int totallen = view_width;
        view_x += (totallen*deltax)/available;
        enforceHardLimits();
        return true;
      }
      if(leftarrow_x <= x && x <= leftarrow_x + 16)
      {
        //clicked on left arrow
        scrollLeft();
        leftarrow_state = 1;
        return true;
      }
      if(rightarrow_x <= x && x <= rightarrow_x + 16)
      {
        scrollRight();
        rightarrow_state = 1;
        return true;
      }
      else
      {
        //clicked the slider
        if(leftarrow_x+16+hbaroff <= x && x <= leftarrow_x+16+hbaroff+hbarlen)
        {
          //clicked the bar itself
          hbarstartx = x;
          hbarstate = 1;
          return true;
        }
        else
        {
          //"teleport"
          //adjust click by half of length of slider
          x -= leftarrow_x+16+hbarlen/2;
          int available = rightarrow_x-(leftarrow_x+16);
          int totallen = view_width;
          //x:available= view_x:totallen
          view_x = (x*totallen)/available;
          enforceHardLimits();
          return true;
        }
      }
    }
  }
  return false;
}