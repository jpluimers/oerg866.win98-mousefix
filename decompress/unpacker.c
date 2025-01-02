/******************************************************************************
 * Copyright (c) 2022 Jaroslav Hensl                                          *
 *                                                                            *
 * Permission is hereby granted, free of charge, to any person                *
 * obtaining a copy of this software and associated documentation             *
 * files (the "Software"), to deal in the Software without                    *
 * restriction, including without limitation the rights to use,               *
 * copy, modify, merge, publish, distribute, sublicense, and/or sell          *
 * copies of the Software, and to permit persons to whom the                  *
 * Software is furnished to do so, subject to the following                   *
 * conditions:                                                                *
 *                                                                            *
 * The above copyright notice and this permission notice shall be             *
 * included in all copies or substantial portions of the Software.            *
 *                                                                            *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,            *
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES            *
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND                   *
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT                *
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,               *
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING               *
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR              *
 * OTHER DEALINGS IN THE SOFTWARE.                                            *
 *                                                                            *
*******************************************************************************/
#include "unpacker.h"

#include "pew.h"

#include <malloc.h>

/**
 * Extract driver form VMM32.VXD or diffent W3/W4 file.
 * 
 * @param src: path to W3/W4 file
 * @param infilename: driver in archive to extract (without *.VXD extension)
 * @param out: path to extact (with filename)
 * @param tmpname: path to temporary file in writeable location
 *
 * @return: PATCH_OK on success otherwise one of PATCH_E_* error code
 ***/
int wx_unpack(const char *src, const char *infilename, const char *out, const char *tmpname)
{
	dos_header_t dos, dos2;
	pe_header_t  pe, pe2;
	pe_w3_t     *w3;
	FILE        *fp, *fp2;
	int          t;
	int status = PATCH_OK;
	int exist_temp = 0;
	
	if(fs_file_exists(tmpname))
	{
		fp = fopen(tmpname, "rb");
		exist_temp = 1;
	}
	else
	{
		fp = fopen(src, "rb");
	}
	
	if(fp)
	{
		t = pe_read(&dos, &pe, fp);
		if(t == PE_W3)
		{
			w3 = pe_w3_read(&dos, &pe, fp);
			if(w3 != NULL)
			{
				char *path_without_ext = fs_path_get(NULL, infilename, "");
				int status_extract = 0;
				
				if(path_without_ext != NULL)
				{
					status_extract = pe_w3_extract(w3, path_without_ext, out);
					fs_path_free(path_without_ext);
				}
				else
				{
					status_extract = pe_w3_extract(w3, infilename, out);
				}
				
				if(status_extract == PE_OK)
				{
					status = PATCH_OK;
				}
				
				pe_w3_free(w3);
			}
			else
			{
				status = PATCH_E_READ;
			}
			
			fclose(fp);
		}
		else if(t == PE_W4)
		{
			fclose(fp);
			
			if(exist_temp)
			{
				status = PATCH_E_CONVERT;
			}
			else
			{	
				if((status = wx_to_w3(src, tmpname)) == PATCH_OK)
				{
					fp2 = fopen(tmpname, "rb");
					if(fp2 != NULL)
					{
						if(pe_read(&dos2, &pe2, fp2) == PE_W3)
						{
							w3 = pe_w3_read(&dos2, &pe2, fp2);
							if(w3 != NULL)
							{
								char *path_without_ext = fs_path_get(NULL, infilename, "");
								int status_extract = 0;
								
								if(path_without_ext != NULL)
								{
									status_extract = pe_w3_extract(w3, path_without_ext, out);
									fs_path_free(path_without_ext);
								}
								else
								{
									status_extract = pe_w3_extract(w3, infilename, out);
								}
								
								if(status_extract == PE_OK)
								{
									status = PATCH_OK;
								}
								
								pe_w3_free(w3);
							}
							else
							{
								//printf("pe_w3_read FAIL\n");
								status = PATCH_E_READ;
							}
						}
						
						fclose(fp2);
						fs_unlink(tmpname);
					}
					else
					{
						status = PATCH_E_READ;
					}
				} // wx_to_w3
			}
		}
		else
		{
			fclose(fp);
			status = PATCH_E_WRONG_TYPE;
		}
	} // fopen
	else
	{
		status = PATCH_E_READ;
	}
	
	return status;
}

/**
 * Convert W4/W3 file to W3 file. If file is already in W3 format only copy it.
 *
 * @param in: input filename
 * @param out: output filename
 *
 * @return: PATCH_OK on success
 **/
int wx_to_w3(const char *in, const char *out)
{
	FILE *fp;
	dos_header_t dos;
	pe_header_t  pe;
	pe_w4_t     *w4;
	int t;
	int status = PATCH_E_CONVERT;
	
	fp = fopen(in, "rb");
	if(fp)
	{
		t = pe_read(&dos, &pe, fp);
		if(t == PE_W4)
		{
			w4 = pe_w4_read(&dos, &pe, fp);
			if(w4 != NULL)
			{
				if(pe_w4_to_w3(w4, out) == PE_OK)
				{
					status = PATCH_OK;;
				}
				
				pe_w4_free(w4);
			}
		}
		else if(t == PE_W3)
		{
			FILE *fw = fopen(out, "wb");
			if(fw)
			{
				fseek(fp, 0, SEEK_SET);
				
				fs_file_copy(fp, fw, 0);
				status = PATCH_OK;
				
				fclose(fw);
			}
		}
		fclose(fp);
	}
	
	return status;
}

/* context listting - VXD */
struct vxd_filelist
{
	pe_w3_t *w3;
	size_t act;
	const char *tmp;
};

/**
 * Open VXD (W3/W4) for file listting, if W4 convert to W3 using tmp
 *
 **/
vxd_filelist_t *vxd_filelist_open(const char *file, const char *tmp)
{
	dos_header_t dos;
	pe_header_t pe;
	int type;
	FILE *fr;
	
	vxd_filelist_t *list = malloc(sizeof(vxd_filelist_t));
	if(list == NULL)
	{
		return NULL;
	}
	
	list->w3 = NULL;
	list->act = 0;
	list->tmp = NULL;
	
	fr = fopen(file, "rb");
	if(!fr)
	{
		free(list);
		return NULL;
	}
	
	type = pe_read(&dos, &pe, fr);
	if(type == PE_W3)
	{
		list->w3 = pe_w3_read(&dos, &pe, fr);
		fclose(fr);
	}
	else if(type == PE_W4)
	{
		fclose(fr);
		if(wx_to_w3(file, tmp) == PATCH_OK)
		{
			fr = fopen(tmp, "rb");
			if(fr)
			{
				type = pe_read(&dos, &pe, fr);
				if(type == PE_W3)
				{
					list->w3 = pe_w3_read(&dos, &pe, fr);
					list->tmp = tmp;
				}
				fclose(fr);
			}
		}
	}
	else
	{
		fclose(fr);
	}
	
	if(list->w3 == NULL)
	{
		free(list);
		return NULL;
	}
	
	return list;
}

/**
 * Return file name and move pointer to another file
 *
 **/
const char *vxd_filelist_get(vxd_filelist_t *list)
{
	static char cname[PE_W3_FILE_NAME_SIZE+1];
	
	if(list->act < list->w3->files_cnt)
	{
		uint8_t *ptr = list->w3->files[list->act].name;
		memcpy(cname, ptr, PE_W3_FILE_NAME_SIZE);
		cname[PE_W3_FILE_NAME_SIZE] = '\0';
		
		list->act++;
		return cname;
	}
	
	return NULL;
}

/**
 * Close the file and delete temp if was used
 *
 **/
void vxd_filelist_close(vxd_filelist_t *list)
{
	if(list->w3 != NULL)
	{
		pe_w3_free(list->w3);
	}
	
	if(list->tmp)
	{
		fs_unlink(list->tmp);
	}
	
	free(list);
}

