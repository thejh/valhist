#include <ringbuffer.h>
#include <sys/user.h>
#include <stdlib.h>
#include <stdio.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xcomposite.h>
#include <string.h>
#include <errno.h>
#include <math.h>
#include <assert.h>
#include <stdint.h>

static void usage(char *argv[]) {
  printf("invocation: %s <number_of_columns> <backlog_size> <height> <min1> <max1> [<min2> <max2> [...]]\n", argv[0]);
}

int main(int argc, char *argv[]) {
  if (argc < 6) {
    usage(argv);
    return 1;
  }
  int number_of_columns = atoi(argv[1]);
  int backlog_size = atoi(argv[2]);
  int height = atoi(argv[3]);
  if (argc != 4 + number_of_columns*2) {
    usage(argv);
    return 1;
  }
  if (number_of_columns < 1 || backlog_size < 2) {
    puts("please use parameters that make sense, thanks");
    usage(argv);
    return 1;
  }
  int argv_i = 4;
  
  double minvals[number_of_columns], maxvals[number_of_columns], scalevals[number_of_columns];
  for (int i=0; i<number_of_columns; i++) {
    char *endptr;
    minvals[i] = strtod(argv[argv_i++], &endptr);
    if (*endptr != 0) {
      puts("a minval is invalid");
      return 1;
    }
    maxvals[i] = strtod(argv[argv_i++], &endptr);
    if (*endptr != 0) {
      puts("a maxval is invalid");
      return 1;
    }
    scalevals[i] = height / (maxvals[i] - minvals[i]);
    printf("configured column %i, will calculate on-screen corrdinate using (val-%lf)*%lf\n", i, minvals[i], scalevals[i]);
  }
  
  int required_bytes = sizeof(double)*backlog_size*number_of_columns;
  //int page_rounded_backlog_size = ((required_bytes+PAGE_SIZE-1)/PAGE_SIZE)*PAGE_SIZE;
  // unused atm
  // int backlog_offset = page_rounded_backlog_size - required_bytes;
  struct ringbuffer rb;
  if (ringbuffer_init(&rb, required_bytes, true)) {
    printf("Error while initializing ringbuffer: %s\n", strerror(errno));
    return 1;
  }
  
  Display *dpy = XOpenDisplay(NULL);
  if (dpy == NULL) {
    puts("can't open display");
    return 1;
  }
  
  /* now do some magic so that we get colors... */
  /* from http://stackoverflow.com/questions/3645632/how-to-create-a-window-with-a-bit-depth-of-32 */
  /*
  int nxvisuals = 0;
  XVisualInfo visual_template;
  visual_template.screen = DefaultScreen(dpy);
  XVisualInfo *visual_list = XGetVisualInfo (dpy, VisualScreenMask, &visual_template, &nxvisuals);
  for (int i=0; i<nxvisuals; i++) {
    printf("  %3d: visual 0x%lx class %d (%s) depth %d\n",
           i,
           visual_list[i].visualid,
           visual_list[i].class,
           visual_list[i].class == TrueColor ? "TrueColor" : "unknown",
           visual_list[i].depth);
  }
  */
  XVisualInfo vinfo;
  if (!XMatchVisualInfo(dpy, XDefaultScreen(dpy), 32, TrueColor, &vinfo)) {
    fprintf(stderr, "no such visual\n");
    return 1;
  }
  printf("Matched visual 0x%lx class %d (%s) depth %d\n",
         vinfo.visualid,
         vinfo.class,
         vinfo.class == TrueColor ? "TrueColor" : "unknown",
         vinfo.depth);
  XSync(dpy, True);
  Visual *visual = vinfo.visual;
  int depth = vinfo.depth;
  XSetWindowAttributes attrs;
  attrs.colormap = XCreateColormap(dpy, XDefaultRootWindow(dpy), visual, AllocNone);
  attrs.background_pixel = 0;
  attrs.border_pixel = 0;
  
  int blackColor = BlackPixel(dpy, DefaultScreen(dpy));
  //int whiteColor = WhitePixel(dpy, DefaultScreen(dpy));
  /* from the old simple call: ", blackColor, whiteColor" */
  Window w = XCreateWindow(dpy, DefaultRootWindow(dpy), 0, 0, backlog_size, height, 0,
                           depth, InputOutput, visual, CWBackPixel | CWColormap | CWBorderPixel, &attrs);
  XSelectInput(dpy, w, StructureNotifyMask);
  XMapWindow(dpy, w);
  GC gc = XCreateGC(dpy, w, 0, NULL);
  XSetForeground(dpy, gc, blackColor);
  for(;;) {
    XEvent e;
    XNextEvent(dpy, &e);
    if (e.type == MapNotify)
      break;
  }
  // so that we don't have to waste 97% CPU time on memmoves.
  // yes, according to Valgrind, it's really that bad.
  struct ringbuffer img_data_rb;
  if (ringbuffer_init(&img_data_rb, backlog_size*height*4, true)) {
    printf("image ringbuffer allocation failed\n");
    return 1;
  }
  //printf("address range of img_data: %p - %p (size %i)\n", img_data, img_data+backlog_size*height, backlog_size*height*4);
  //memset(img_data, 0, backlog_size*height*sizeof(uint32_t));
  // hmm... seems like the "depth" is not colordepth, but layers depth? see PutImage.c:742 in Xlib
  // no, it can't be!
  // GRAH, this drives me crazy!
  // hmm, seems as if I have to use ZPixmap, not XYPixmap!
  XImage *img = XCreateImage(dpy, visual, depth, ZPixmap, 0, (char *)img_data_rb.start, backlog_size, height, 32, 4*backlog_size);
  assert(img != NULL);
  XFlush(dpy);
  
  while (1) {
    double *current_pos = rb.start;
    for (int i=0; i<number_of_columns; i++) {
      double n;
      if (fscanf(stdin, "%lf", &n) != 1) {
        puts("fscanf fail");
        return 1;
      }
      current_pos[i] = n;
    }
    /* we could optimize this because the pagesize can be divided by sizeof(double),
     * but meh, we probably don't need that performance
     */
    ringbuffer_bump(&rb, sizeof(double)*number_of_columns);
    
    /* now update our image... */
    /* move the image one to the left */
    /* move everything one to the left... */
    ringbuffer_bump(&img_data_rb, 4);
    int *img_data = img_data_rb.start;
    // memmove(img_data, img_data+1, backlog_size*height*4); /* this was slow; removed */
    /* ... and make the last pixel black */
    for (int y=0; y<height; y++) {
      *(img_data+(backlog_size*y+backlog_size-1)) = 0;
    }
    /* draw the new values (at the right side) */
    for (int i=0; i<number_of_columns; i++) {
      int scaled_y = (int) round((current_pos[i]-minvals[i])*scalevals[i]);
      int underflow;
      if ((underflow=scaled_y<0) || scaled_y>=height) {
        printf("WARNING: %s in column %i (resulting y is %i)\n", underflow?"underflow":"overflow", i, scaled_y);
        continue;
      }
      // flip vertically (turn upside down)
      scaled_y = (height-1) - scaled_y;
      *(img_data+backlog_size*scaled_y+(backlog_size-1)) = 0xffffff00;
    }
    /* upload the new image */
    img->data = (char *)img_data;
    XPutImage(dpy, w, gc, img, 0, 0, 0, 0, backlog_size, height);
    XFlush(dpy);
  }
}
