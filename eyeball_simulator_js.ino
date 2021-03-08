#include <TVout.h>

#include "sclera_data_22x22.h"
#include "iris_data_22x22.h"
#include "iris_mask_data_22x22.h"


TVout TV;

// storage for image buffers
#define buffer_size 22*((22+7)/8)
static char image_data[22*((22+7)/8)];
static char temp_data[22*((22+7)/8)];
static char eyelid1_data[buffer_size];

extern const unsigned char white_pixel[];

extern const unsigned char black_pixel[];

// packed binary image
struct PBM
{
  char rows, cols;
  char bpl; // bytes per line
  char* data;

  PBM(char rows, char cols, char *data)
    :rows(rows), cols(cols), bpl((cols+7)/8), data(data)
  {
  }

  ~PBM()
  {
  }

  unsigned char get_pixel(char row, char col)
  {
    return (data[row*bpl + col/8] & (1 << (7 - (col & 7)))) == 0 ? 0 : 1;
  }

  void set_pixel(char row, char col, unsigned char value)
  {
    if (value){
      data[row*bpl + col/8] |=  (1 << (7 - (col & 7)));
    } else {
      data[row*bpl + col/8] &= ~(1 << (7 - (col & 7)));
    }
  }

  void clear(char value=0)
  {
    for (int i=0; i<((int)rows)*bpl; i++){
      data[i] = value;
    }
  }
};

// bit block transfer: copy source image to dest image where indicated by
// mask image, offset in x and y directions
void blit(PBM& dest,
    PBM& source,
    PBM& mask,
    char x_offset,
    char y_offset,
    char invert_mask=0)
{
  for (char r=0; r<source.rows; r++){
    char row = r + y_offset;
    if (row >=0 && row < dest.rows){
      for (char c=0; c<source.cols; c++){
        char col = c + x_offset;
        if (col >= 0 && col < dest.cols){
          if (mask.get_pixel(r, c) ^ invert_mask){
            dest.set_pixel(row, col, source.get_pixel(r, c));
          }
        }
      }
    }
  }
}

// draw a disk (or outside of a disk)
void disk(PBM& image,
    char cx,
    char cy,
    char radius,
    char value,
    int flip=1)
{
  radius *= radius;
  for (char row=0; row<image.rows; row++){
    for (char col=0; col<image.cols; col++){
      int r = ((row - cy)* (row - cy)
                 + (col - cx) * (col - cx));
      if ((r <= radius) ^ flip){
        image.set_pixel(row, col, value);
      }
    }
  }
}

// make a mask for the open percentage of the eye
void make_lid_mask(PBM& image, float percent_open)
{
  float lid_r = 11;
  float s = (100/(percent_open+1))*(100/(percent_open+1));
  for (char row=0; row<image.rows; row++){
    for (char col=0; col<image.cols; col++){
      float r = (s*(row - float(image.rows)/2+0.5) *
                 (row - float(image.rows)/2+0.5) +
                 (col - float(image.cols)/2+0.5) *
                 (col - float(image.cols)/2+0.5));
      if (r > lid_r*lid_r){
        image.set_pixel(row, col, 0);
      } else {
        image.set_pixel(row, col, 1);
      }
    }
  }
}


// draw eye to TV buffer
void draw_eye(PBM& image,
              PBM& temp,
              //PBM& eyelid1,
              int horiz,
              int vert,
              float pupil_size,
              float percent_open)
{
  float glint_h_offset = -2.5; // position of eye glint
  float glint_v_offset = -2.5;
  float glint_size = 1;   // radius of eye glint

  image.clear(0);

  // add the sclera image
  PBM sclera(22, 22, sclera_data);
  blit(image, sclera, sclera, 0, 0);

  // add the iris image, offset appropriately
  PBM iris(22, 22, iris_data);
  PBM iris_mask(22, 22, iris_mask_data);
  blit(image, iris, iris_mask, horiz, vert);  

  // add the pupil, note that it moves 1.5x as much as the iris
  temp.clear(0);
  disk(temp,
       float(temp.cols)/2 + horiz/2 - 0.5,
       float(temp.rows)/2 + vert/2 - 0.5,
       pupil_size, 1, 1);
  blit(image, temp, temp, horiz, vert, 1);

  // add the glint
  temp.clear(0);
  disk(temp,
       float(temp.cols)/2 + glint_h_offset,
       float(temp.rows)/2 + glint_v_offset,
       glint_size, 1, 0);
  blit(image, temp, temp, horiz, vert, 0);

  // close eye to specified percent  
  //make_lid_mask(temp, percent_open); //comment these out later
 // blit(image, eyelid1, eyelid1, 0, 0, 1); //comment these out later
  
// copy to TV screen
  int n = 3, gap = 1;
  for (char row=0; row<image.rows; row++){
    for (char col=0; col<image.cols; col++){
      // For each image.get_pixel(row, col)
      // Call TV.bitmap() depending on color
      if (image.get_pixel(row,col))
      {
        TV.bitmap(col*4, row*4, white_pixel);
      }
      else
      {
        TV.bitmap(col*4, row*4, black_pixel);
      }
    /*
      for (char r=0;r<n+gap;r++){
        for (char c=0;c<n+gap;c++){
          if (r && c){
            TV.set_pixel(col*(n+gap)+c, row*(n+gap)+r,
                         image.get_pixel(row, col));
          } else {
            TV.set_pixel(col*(n+gap)+c, row*(n+gap)+r, 0);
          } 
        }
      }
    */
    }
  }
}


void setup()  {
  TV.begin(NTSC, 120, 96);
}


void loop() {

  // create structures for the working images
  PBM image(22, 22, image_data);
  PBM temp(22, 22, temp_data);
  //PBM eyelid1(22, 22, eyelid1_data);

  TV.clear_screen();

  // the parameters of the eye
  char horiz = 0;             // -4..+4
  char vert = 0;              // -4..+4
  float pupil_size = 3.f;     // 0-10(?)
  float percent_open = 100.f; // 0-100

  // Make eye lid mask before running main loop
 // make_lid_mask(eyelid1, 80.f);

  // simple loop to draw the eye in various states as a demo
  while(1){
   
   int hor_value = analogRead(A0);
   int vert_value = analogRead(A1);
   int hor_position = map(hor_value,0,1023,-4,4);
   int vert_position = map(vert_value,0,1023,-4,4);
        horiz = hor_position;
        vert = vert_position;
        pupil_size = abs(5-hor_position)/2;
        percent_open = 75.f;
        draw_eye(image, temp, horiz, vert, pupil_size, percent_open);
      }
    }


PROGMEM const unsigned char white_pixel[] = {
  4,4
,0xe0
,0xe0
,0xe0
,0x00
};

PROGMEM const unsigned char black_pixel[] = {
  4,4
,0x00
,0x00
,0x00
,0x00
};
