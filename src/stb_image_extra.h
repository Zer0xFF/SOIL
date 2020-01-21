
static stbi_uc *hdr_load_rgbe(stbi__context *s, int *x, int *y, int *comp, int req_comp)
{
   char buffer[STBI__HDR_BUFLEN];
	char *token;
	int valid = 0;
	int width, height;
   stbi_uc *scanline;
	stbi_uc *rgbe_data;
	int len;
	unsigned char count, value;
	int i, j, k, c1,c2, z;


	// Check identifier
	if (strcmp(stbi__hdr_gettoken(s,buffer), "#?RADIANCE") != 0)
		return stbi__err("not HDR", "Corrupt HDR image");

	// Parse header
	while(1) {
		token = stbi__hdr_gettoken(s,buffer);
      if (token[0] == 0) break;
		if (strcmp(token, "FORMAT=32-bit_rle_rgbe") == 0) valid = 1;
   }

	if (!valid)    return stbi__err("unsupported format", "Unsupported HDR format");

   // Parse width and height
   // can't use sscanf() if we're not using stdio!
   token = stbi__hdr_gettoken(s,buffer);
   if (strncmp(token, "-Y ", 3))  return stbi__err("unsupported data layout", "Unsupported HDR format");
   token += 3;
   height = strtol(token, &token, 10);
   while (*token == ' ') ++token;
   if (strncmp(token, "+X ", 3))  return stbi__err("unsupported data layout", "Unsupported HDR format");
   token += 3;
   width = strtol(token, NULL, 10);

	*x = width;
	*y = height;

	// RGBE _MUST_ come out as 4 components
   *comp = 4;
	req_comp = 4;

	// Read data
	rgbe_data = (stbi_uc *) malloc(height * width * req_comp * sizeof(stbi_uc));
	//	point to the beginning
	scanline = rgbe_data;

	// Load image data
   // image data is stored as some number of scan lines
	if( width < 8 || width >= 32768) {
		// Read flat data
      for (j=0; j < height; ++j) {
         for (i=0; i < width; ++i) {
           main_decode_loop:
            //stbi__getn(rgbe, 4);
            stbi__getn(s,scanline, 4);
			scanline += 4;
         }
      }
	} else {
		// Read RLE-encoded data
		for (j = 0; j < height; ++j) {
         c1 = stbi__get8(s);
         c2 = stbi__get8(s);
         len = stbi__get8(s);
         if (c1 != 2 || c2 != 2 || (len & 0x80)) {
            // not run-length encoded, so we have to actually use THIS data as a decoded
            // pixel (note this can't be a valid pixel--one of RGB must be >= 128)
            scanline[0] = c1;
            scanline[1] = c2;
            scanline[2] = len;
            scanline[3] = stbi__get8(s);
            scanline += 4;
            i = 1;
            j = 0;
            goto main_decode_loop; // yes, this is insane; blame the insane format
         }
         len <<= 8;
         len |= stbi__get8(s);
         if (len != width) { free(rgbe_data); return stbi__err("invalid decoded scanline length", "corrupt HDR"); }
			for (k = 0; k < 4; ++k) {
				i = 0;
				while (i < width) {
					count = stbi__get8(s);
					if (count > 128) {
						// Run
						value = stbi__get8(s);
                  count -= 128;
						for (z = 0; z < count; ++z)
							scanline[i++ * 4 + k] = value;
					} else {
						// Dump
						for (z = 0; z < count; ++z)
							scanline[i++ * 4 + k] = stbi__get8(s);
					}
				}
			}
			//	move the scanline on
			scanline += 4 * width;
		}
	}

   return rgbe_data;
}

#ifndef STBI_NO_STDIO
float *stbi_hdr_load_from_file(FILE *f, int *x, int *y, int *comp, int req_comp)
{
   stbi__context s;
   stbi__result_info ri;
   stbi__start_file(&s,f);
   return stbi__hdr_load(&s,x,y,comp,req_comp, &ri);
}

stbi_uc *stbi_hdr_load_rgbe_file(FILE *f, int *x, int *y, int *comp, int req_comp)
{
   stbi__context s;
   stbi__start_file(&s,f);
   return hdr_load_rgbe(&s,x,y,comp,req_comp);
}

stbi_uc *stbi_hdr_load_rgbe        (char const *filename,           int *x, int *y, int *comp, int req_comp)
{
   FILE *f = fopen(filename, "rb");
   unsigned char *result;
   if (!f) return stbi__err("can't fopen", "Unable to open file");
   result = stbi_hdr_load_rgbe_file(f,x,y,comp,req_comp);
   fclose(f);
   return result;
}
#endif


#ifndef STBI_NO_WRITE

static void write8(FILE *f, int x) { uint8_t z = (uint8_t) x; fwrite(&z,1,1,f); }

static void writefv(FILE *f, char *fmt, va_list v)
{
   while (*fmt) {
      switch (*fmt++) {
         case ' ': break;
         case '1': { uint8_t x = va_arg(v, int); write8(f,x); break; }
         case '2': { int16_t x = va_arg(v, int); write8(f,x); write8(f,x>>8); break; }
         case '4': { int32_t x = va_arg(v, int); write8(f,x); write8(f,x>>8); write8(f,x>>16); write8(f,x>>24); break; }
         default:
            assert(0);
            va_end(v);
            return;
      }
   }
}

static void writef(FILE *f, char *fmt, ...)
{
   va_list v;
   va_start(v, fmt);
   writefv(f,fmt,v);
   va_end(v);
}

static void write_pixels(FILE *f, int rgb_dir, int vdir, int x, int y, int comp, void *data, int write_alpha, int scanline_pad)
{
   uint8_t bg[3] = { 255, 0, 255}, px[3];
   uint32_t zero = 0;
   int i,j,k, j_end;

   if (vdir < 0)
      j_end = -1, j = y-1;
   else
      j_end =  y, j = 0;

   for (; j != j_end; j += vdir) {
      for (i=0; i < x; ++i) {
         uint8_t *d = (uint8_t *) data + (j*x+i)*comp;
         if (write_alpha < 0)
            fwrite(&d[comp-1], 1, 1, f);
         switch (comp) {
            case 1:
            case 2: writef(f, "111", d[0],d[0],d[0]);
                    break;
            case 4:
               if (!write_alpha) {
                  for (k=0; k < 3; ++k)
                     px[k] = bg[k] + ((d[k] - bg[k]) * d[3])/255;
                  writef(f, "111", px[1-rgb_dir],px[1],px[1+rgb_dir]);
                  break;
               }
               /* FALLTHROUGH */
            case 3:
               writef(f, "111", d[1-rgb_dir],d[1],d[1+rgb_dir]);
               break;
         }
         if (write_alpha > 0)
            fwrite(&d[comp-1], 1, 1, f);
      }
      fwrite(&zero,scanline_pad,1,f);
   }
}

static int outfile(char const *filename, int rgb_dir, int vdir, int x, int y, int comp, void *data, int alpha, int pad, char *fmt, ...)
{
   FILE *f = fopen(filename, "wb");
   if (f) {
      va_list v;
      va_start(v, fmt);
      writefv(f, fmt, v);
      va_end(v);
      write_pixels(f,rgb_dir,vdir,x,y,comp,data,alpha,pad);
      fclose(f);
   }
   return f != NULL;
}

int stbi_write_bmp(char const *filename, int x, int y, int comp, void *data)
{
   int pad = (-x*3) & 3;
   return outfile(filename,-1,-1,x,y,comp,data,0,pad,
           "11 4 22 4" "4 44 22 444444",
           'B', 'M', 14+40+(x*3+pad)*y, 0,0, 14+40,  // file header
            40, x,y, 1,24, 0,0,0,0,0,0);             // bitmap header
}

int stbi_write_tga(char const *filename, int x, int y, int comp, void *data)
{
   int has_alpha = !(comp & 1);
   return outfile(filename, -1,-1, x, y, comp, data, has_alpha, 0,
                  "111 221 2222 11", 0,0,2, 0,0,0, 0,0,x,y, 24+8*has_alpha, 8*has_alpha);
}

// any other image formats that do interleaved rgb data?
//    PNG: requires adler32,crc32 -- significant amount of code
//    PSD: no, channels output separately
//    TIFF: no, stripwise-interleaved... i think

#endif // STBI_NO_WRITE