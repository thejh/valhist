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
#include <sys/ipc.h>
#include <sys/shm.h>
#include <X11/extensions/XShm.h>

static void usage(char *argv[]) {
  printf("invocation: %s <number_of_columns> <backlog_size> <height> <invspeed> <frameskip> <min1> <max1> [<min2> <max2> [...]]\n", argv[0]);
}

int main(int argc, char *argv[]) {
  if (argc < 8) {
    usage(argv);
    return 1;
  }
  int number_of_columns = atoi(argv[1]);
  int backlog_size = atoi(argv[2]);
  int height = atoi(argv[3]);
  int invspeed = atoi(argv[4]);
  int frameskip = atoi(argv[5]);
  if (argc != 6 + number_of_columns*2) {
    usage(argv);
    return 1;
  }
  if (number_of_columns < 1 || backlog_size < 2 || invspeed < 1 || frameskip < 1) {
    puts("please use parameters that make sense, thanks");
    usage(argv);
    return 1;
  }
  int argv_i = 6;
  
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
    printf("configured column %i"
         "\nminval=%lf, maxval=%lf"
         "\nwill calculate on-screen corrdinate using (val-%lf)*%lf\n"
	 "\n",
	 i, minvals[i], maxvals[i], minvals[i], scalevals[i]);
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
  // hmm... seems like the "depth" is not colordepth, but layers depth? see PutImage.c:742 in Xlib
  // no, it can't be!
  // GRAH, this drives me crazy!
  // hmm, seems as if I have to use ZPixmap, not XYPixmap!
  XShmSegmentInfo shminfo;
  XImage *img = XShmCreateImage(dpy, visual, depth, ZPixmap, NULL, &shminfo, backlog_size, height);
  assert(img != NULL);
  shminfo.shmid = shmget(IPC_PRIVATE, img->bytes_per_line*img->height, IPC_CREAT|0777);
  shminfo.shmaddr = img->data = shmat(shminfo.shmid, 0, 0);
  shminfo.readOnly = False;
  XShmAttach(dpy, &shminfo);
  XFlush(dpy);

  int invspeed_i = 0, frameskip_i = 0;

  while (1) {
    double rawvals[number_of_columns];
    for (int i=0; i<number_of_columns; i++) {
      double n;
      if (fscanf(stdin, "%lf", &n) != 1) {
        puts("fscanf fail");
        return 1;
      }

      rawvals[i] = n;
    }

    if (invspeed_i == 0) {
      /* now update our image... */
      /* move the image one to the left */
      /* move everything one to the left... */
      memmove(img->data, img->data+4, img->bytes_per_line*img->height-4);
    }
    int *img_data = (void *)img->data;
    if (invspeed_i == 0) {
      /* ... and make the last pixel black */
      for (int y=0; y<height; y++) {
        *(img_data+(backlog_size*y+backlog_size-1)) = 0;
      }
    }
    if (++invspeed_i == invspeed) invspeed_i = 0;
    
    /* draw the new values (at the right side) */
    for (int i=0; i<number_of_columns; i++) {
      int scaled_y = (int) round((rawvals[i]-minvals[i])*scalevals[i]);
      int underflow;
      if ((underflow=scaled_y<0) || scaled_y>=height) {
        printf("WARNING: %s in column %i (minval is %f, scaleval is %f, resulting y is %i)\n", underflow?"underflow":"overflow", i, minvals[i], scalevals[i], scaled_y);
        continue;
      }
      // flip vertically (turn upside down)
      scaled_y = (height-1) - scaled_y;
      *(img_data+backlog_size*scaled_y+(backlog_size-1)) = 0xffffff00;
    }
    /* upload the new image */
    if (invspeed_i == 0 && ++frameskip_i == frameskip) {
      img->data = (char *)img_data;
      XShmPutImage(dpy, w, gc, img, 0, 0, 0, 0, backlog_size, height, False);
      XFlush(dpy);
      frameskip_i = 0;
    }
  }
}
