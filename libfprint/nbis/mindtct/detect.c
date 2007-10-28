/*******************************************************************************

License: 
This software was developed at the National Institute of Standards and 
Technology (NIST) by employees of the Federal Government in the course 
of their official duties. Pursuant to title 17 Section 105 of the 
United States Code, this software is not subject to copyright protection 
and is in the public domain. NIST assumes no responsibility  whatsoever for 
its use by other parties, and makes no guarantees, expressed or implied, 
about its quality, reliability, or any other characteristic. 

Disclaimer: 
This software was developed to promote biometric standards and biometric
technology testing for the Federal Government in accordance with the USA
PATRIOT Act and the Enhanced Border Security and Visa Entry Reform Act.
Specific hardware and software products identified in this software were used
in order to perform the software development.  In no case does such
identification imply recommendation or endorsement by the National Institute
of Standards and Technology, nor does it imply that the products and equipment
identified are necessarily the best available for the purpose.  

*******************************************************************************/

/***********************************************************************
      LIBRARY: LFS - NIST Latent Fingerprint System

      FILE:    DETECT.C
      AUTHOR:  Michael D. Garris
      DATE:    08/16/1999
      UPDATED: 10/04/1999 Version 2 by MDG
      UPDATED: 03/16/2005 by MDG

      Takes an 8-bit grayscale fingerpinrt image and detects minutiae
      as part of the NIST Latent Fingerprint System (LFS).

***********************************************************************
               ROUTINES:
                        lfs_detect_minutiae_V2()

***********************************************************************/

#include <stdio.h>
#include <string.h>
#include <lfs.h>
#include <log.h>

/*************************************************************************
#cat: lfs_detect_minutiae_V2 - Takes a grayscale fingerprint image (of
#cat:          arbitrary size), and returns a set of image block maps,
#cat:          a binarized image designating ridges from valleys,
#cat:          and a list of minutiae (including position, reliability,
#cat:          type, direction, neighbors, and ridge counts to neighbors).
#cat:          The image maps include a ridge flow directional map,
#cat:          a map of low contrast blocks, a map of low ridge flow blocks.
#cat:          and a map of high-curvature blocks.

   Input:
      idata     - input 8-bit grayscale fingerprint image data
      iw        - width (in pixels) of the image
      ih        - height (in pixels) of the image
      lfsparms  - parameters and thresholds for controlling LFS

   Output:
      ominutiae - resulting list of minutiae
      odmap     - resulting Direction Map
                  {invalid (-1) or valid ridge directions}
      olcmap    - resulting Low Contrast Map
                  {low contrast (TRUE), high contrast (FALSE)}
      olfmap    - resulting Low Ridge Flow Map
                  {low ridge flow (TRUE), high ridge flow (FALSE)}
      ohcmap    - resulting High Curvature Map
                  {high curvature (TRUE), low curvature (FALSE)}
      omw       - width (in blocks) of image maps
      omh       - height (in blocks) of image maps
      obdata    - resulting binarized image
                  {0 = black pixel (ridge) and 255 = white pixel (valley)}
      obw       - width (in pixels) of the binary image
      obh       - height (in pixels) of the binary image
   Return Code:
      Zero      - successful completion
      Negative  - system error
**************************************************************************/
int lfs_detect_minutiae_V2(MINUTIAE **ominutiae,
                        int **odmap, int **olcmap, int **olfmap, int **ohcmap,
                        int *omw, int *omh,
                        unsigned char **obdata, int *obw, int *obh,
                        unsigned char *idata, const int iw, const int ih,
                        const LFSPARMS *lfsparms)
{
   unsigned char *pdata, *bdata;
   int pw, ph, bw, bh;
   DIR2RAD *dir2rad;
   DFTWAVES *dftwaves;
   ROTGRIDS *dftgrids;
   ROTGRIDS *dirbingrids;
   int *direction_map, *low_contrast_map, *low_flow_map, *high_curve_map;
   int mw, mh;
   int ret, maxpad;
   MINUTIAE *minutiae;

   /******************/
   /* INITIALIZATION */
   /******************/

   /* If LOG_REPORT defined, open log report file. */
   if((ret = open_logfile()))
      /* If system error, exit with error code. */
      return(ret);

   /* Determine the maximum amount of image padding required to support */
   /* LFS processes.                                                    */
   maxpad = get_max_padding_V2(lfsparms->windowsize, lfsparms->windowoffset,
                          lfsparms->dirbin_grid_w, lfsparms->dirbin_grid_h);

   /* Initialize lookup table for converting integer directions */
   /* to angles in radians.                                     */
   if((ret = init_dir2rad(&dir2rad, lfsparms->num_directions))){
      /* Free memory allocated to this point. */
      return(ret);
   }

   /* Initialize wave form lookup tables for DFT analyses. */
   /* used for direction binarization.                             */
   if((ret = init_dftwaves(&dftwaves, dft_coefs, lfsparms->num_dft_waves,
                        lfsparms->windowsize))){
      /* Free memory allocated to this point. */
      free_dir2rad(dir2rad);
      return(ret);
   }

   /* Initialize lookup table for pixel offsets to rotated grids */
   /* used for DFT analyses.                                     */
   if((ret = init_rotgrids(&dftgrids, iw, ih, maxpad,
                        lfsparms->start_dir_angle, lfsparms->num_directions,
                        lfsparms->windowsize, lfsparms->windowsize,
                        RELATIVE2ORIGIN))){
      /* Free memory allocated to this point. */
      free_dir2rad(dir2rad);
      free_dftwaves(dftwaves);
      return(ret);
   }

   /* Pad input image based on max padding. */
   if(maxpad > 0){   /* May not need to pad at all */
      if((ret = pad_uchar_image(&pdata, &pw, &ph, idata, iw, ih,
                             maxpad, lfsparms->pad_value))){
         /* Free memory allocated to this point. */
         free_dir2rad(dir2rad);
         free_dftwaves(dftwaves);
         free_rotgrids(dftgrids);
         return(ret);
      }
   }
   else{
      /* If padding is unnecessary, then copy the input image. */
      pdata = (unsigned char *)malloc(iw*ih);
      if(pdata == (unsigned char *)NULL){
         /* Free memory allocated to this point. */
         free_dir2rad(dir2rad);
         free_dftwaves(dftwaves);
         free_rotgrids(dftgrids);
         fprintf(stderr, "ERROR : lfs_detect_minutiae_V2 : malloc : pdata\n");
         return(-580);
      }
      memcpy(pdata, idata, iw*ih);
      pw = iw;
      ph = ih;
   }

   /* Scale input image to 6 bits [0..63] */
   /* !!! Would like to remove this dependency eventualy !!!     */
   /* But, the DFT computations will need to be changed, and     */
   /* could not get this work upon first attempt. Also, if not   */
   /* careful, I think accumulated power magnitudes may overflow */
   /* doubles.                                                   */
   bits_8to6(pdata, pw, ph);

   print2log("\nINITIALIZATION AND PADDING DONE\n");

   /******************/
   /*      MAPS      */
   /******************/

   /* Generate block maps from the input image. */
   if((ret = gen_image_maps(&direction_map, &low_contrast_map,
                    &low_flow_map, &high_curve_map, &mw, &mh,
                    pdata, pw, ph, dir2rad, dftwaves, dftgrids, lfsparms))){
      /* Free memory allocated to this point. */
      free_dir2rad(dir2rad);
      free_dftwaves(dftwaves);
      free_rotgrids(dftgrids);
      free(pdata);
      return(ret);
   }
   /* Deallocate working memories. */
   free_dir2rad(dir2rad);
   free_dftwaves(dftwaves);
   free_rotgrids(dftgrids);

   print2log("\nMAPS DONE\n");

   /******************/
   /* BINARIZARION   */
   /******************/

   /* Initialize lookup table for pixel offsets to rotated grids */
   /* used for directional binarization.                         */
   if((ret = init_rotgrids(&dirbingrids, iw, ih, maxpad,
                        lfsparms->start_dir_angle, lfsparms->num_directions,
                        lfsparms->dirbin_grid_w, lfsparms->dirbin_grid_h,
                        RELATIVE2CENTER))){
      /* Free memory allocated to this point. */
      free(pdata);
      free(direction_map);
      free(low_contrast_map);
      free(low_flow_map);
      free(high_curve_map);
      return(ret);
   }

   /* Binarize input image based on NMAP information. */
   if((ret = binarize_V2(&bdata, &bw, &bh,
                      pdata, pw, ph, direction_map, mw, mh,
                      dirbingrids, lfsparms))){
      /* Free memory allocated to this point. */
      free(pdata);
      free(direction_map);
      free(low_contrast_map);
      free(low_flow_map);
      free(high_curve_map);
      free_rotgrids(dirbingrids);
      return(ret);
   }

   /* Deallocate working memory. */
   free_rotgrids(dirbingrids);

   /* Check dimension of binary image.  If they are different from */
   /* the input image, then ERROR.                                 */
   if((iw != bw) || (ih != bh)){
      /* Free memory allocated to this point. */
      free(pdata);
      free(direction_map);
      free(low_contrast_map);
      free(low_flow_map);
      free(high_curve_map);
      free(bdata);
      fprintf(stderr, "ERROR : lfs_detect_minutiae_V2 :");
      fprintf(stderr,"binary image has bad dimensions : %d, %d\n",
              bw, bh);
      return(-581);
   }

   print2log("\nBINARIZATION DONE\n");

   /******************/
   /*   DETECTION    */
   /******************/

   /* Convert 8-bit grayscale binary image [0,255] to */
   /* 8-bit binary image [0,1].                       */
   gray2bin(1, 1, 0, bdata, iw, ih);

   /* Allocate initial list of minutia pointers. */
   if((ret = alloc_minutiae(&minutiae, MAX_MINUTIAE))){
      return(ret);
   }

   /* Detect the minutiae in the binarized image. */
   if((ret = detect_minutiae_V2(minutiae, bdata, iw, ih,
                             direction_map, low_flow_map, high_curve_map,
                             mw, mh, lfsparms))){
      /* Free memory allocated to this point. */
      free(pdata);
      free(direction_map);
      free(low_contrast_map);
      free(low_flow_map);
      free(high_curve_map);
      free(bdata);
      return(ret);
   }

   if((ret = remove_false_minutia_V2(minutiae, bdata, iw, ih,
                       direction_map, low_flow_map, high_curve_map, mw, mh,
                       lfsparms))){
      /* Free memory allocated to this point. */
      free(pdata);
      free(direction_map);
      free(low_contrast_map);
      free(low_flow_map);
      free(high_curve_map);
      free(bdata);
      free_minutiae(minutiae);
      return(ret);
   }

   print2log("\nMINUTIA DETECTION DONE\n");

   /******************/
   /*  RIDGE COUNTS  */
   /******************/
   if((ret = count_minutiae_ridges(minutiae, bdata, iw, ih, lfsparms))){
      /* Free memory allocated to this point. */
      free(pdata);
      free(direction_map);
      free(low_contrast_map);
      free(low_flow_map);
      free(high_curve_map);
      free_minutiae(minutiae);
      return(ret);
   }


   print2log("\nNEIGHBOR RIDGE COUNT DONE\n");

   /******************/
   /*    WRAP-UP     */
   /******************/

   /* Convert 8-bit binary image [0,1] to 8-bit */
   /* grayscale binary image [0,255].           */
   gray2bin(1, 255, 0, bdata, iw, ih);

   /* Deallocate working memory. */
   free(pdata);

   /* Assign results to output pointers. */
   *odmap = direction_map;
   *olcmap = low_contrast_map;
   *olfmap = low_flow_map;
   *ohcmap = high_curve_map;
   *omw = mw;
   *omh = mh;
   *obdata = bdata;
   *obw = bw;
   *obh = bh;
   *ominutiae = minutiae;

   /* If LOG_REPORT defined, close log report file. */
   if((ret = close_logfile()))
      return(ret);

   return(0);
}

