// SPDX-License-Identifier: GPL-2.0
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <locale.h>

#include "errorhelper.h"
#include "ssrf.h"
#include "subsurface-string.h"
#include "membuffer.h"
#include "gettext.h"
#include "divelist.h"
#include "file.h"
#include "libdivecomputer.h"
#include "divesite.h"

#define DEBUG FFFFFFFF
extern char *add_to_string_w_sep(char *orig, const char *sep, const char *fmt, ...);
/*
 * dc model definitions
 */
#define SMARTPRO          0x10
#define GALILEO           0x11
#define ALADINTEC         0x12
#define ALADINTEC2G       0x13
#define SMARTCOM          0x14
#define ALADIN2G          0x15
#define ALADINSPORTMATRIX 0x17
#define SMARTTEC          0x18
#define GALILEOTRIMIX     0x19
#define SMARTZ            0x1C
#define MERIDIAN          0x20
#define ALADINSQUARE      0x22
#define CHROMIS           0x24
#define ALADINA1          0x25
#define MANTIS2           0x26
#define G2                0x32
#define G2HUD             0x42

/*
 * Data positions in serial stream as expected by libdc
 */
#define LIBDC_TZ_POS		3
#define LIBDC_TZ		16
#define LIBDC_MAX_DEPTH		22
#define LIBDC_DIVE_TIME		26
#define LIBDC_MAX_TEMP		28
#define LIBDC_MIN_TEMP		30
#define LIBDC_SURF_TEMP		32
#define LIBDC_GASMIX		44
#define LIBDC_TANK_PRESS	50

#define LIBDC_SAMPLES_MANTIS	152
#define LIBDC_SAMPLES_G2	84
#define ASD_SAMPLES		183

/*
 * Data positions in asd raw data buffer
 */
#define MANTIS_MAXDEPTH		27
#define MANTIS_DIVE_TIME	32
#define MANTIS_MAX_TEMP		20
#define MANTIS_MIN_TEMP		34
#define MANTIS_SURF_TEMP	18
#define MANTIS_GAS_MIX		36
#define MANTIS_TANK_PRESS	44

#define G2_MAXDEPTH		30
#define G2_DIVE_TIME		32
#define G2_MAX_TEMP		148
#define G2_MIN_TEMP		34
#define G2_SURF_TEMP		18

/*
 * Returns a dc_descriptor_t structure based on dc  model's number.
 * This ensures the model pased to libdc_buffer_parser() is a supported model and avoids
 * problems with shared model num devices by taking the family into account.  The family
 * is estimated elsewhere based in dive header length.
 */
static dc_descriptor_t *get_data_descriptor(int data_model, dc_family_t data_fam)
{
	dc_descriptor_t *descriptor = NULL, *current = NULL;
	dc_iterator_t *iterator = NULL;
	dc_status_t rc;

	rc = dc_descriptor_iterator(&iterator);
	if (rc != DC_STATUS_SUCCESS) {
		report_error("[Error][libdc]\t\t\tCreating the device descriptor iterator.\n");
		return current;
	}
	while ((dc_iterator_next(iterator, &descriptor)) == DC_STATUS_SUCCESS) {
		int desc_model = dc_descriptor_get_model(descriptor);
		dc_family_t desc_fam = dc_descriptor_get_type(descriptor);

		if (data_model == desc_model && (desc_fam == DC_FAMILY_UWATEC_SMART || desc_fam == DC_FAMILY_UWATEC_MERIDIAN)) {
			current = descriptor;
			break;
		}
		dc_descriptor_free(descriptor);
	}
	dc_iterator_free(iterator);
	return current;
}

static int prepare_data(int data_model, dc_family_t dc_fam, device_data_t *dev_data)
{
	dev_data->device = NULL;
	dev_data->context = NULL;
	dev_data->descriptor = get_data_descriptor(data_model, dc_fam);
	if (dev_data->descriptor) {
		dev_data->vendor = dc_descriptor_get_vendor(dev_data->descriptor);
		dev_data->product = dc_descriptor_get_product(dev_data->descriptor);
		dev_data->model = add_to_string(dev_data->model, "%s %s", dev_data->vendor, dev_data->product);
#ifdef DEBUG
	fprintf(stderr, "dc model = %2x\n", data_model);
#endif
		return DC_STATUS_SUCCESS;
	} else {
		return DC_STATUS_UNSUPPORTED;
	}
}

/*
 * This function returns an allocated memory buffer with the completed dc data.
 * The buffer has to be padded in the beginning with the header divecomputer expects to find,
 * this is  a5a55a5a.
 * BTW .asd file includes a modified data structure for the dc data, which is not the same
 * expected by libdc. So, we need to manually relocate those parts of the buffer needed by
 * libdivecomputer.
 */
unsigned char *build_dc_data(int model, unsigned char *input, int max, int *out_size)
{
	unsigned char *ptr = input, *buffer, head_begin[] = {0xa5, 0xa5, 0x5a, 0x5a};
	int buf_size = 0;

	buf_size = ((ptr[1] << 8) + ptr[0]); 	// get the size of the data for libdivecomputer
	buffer =  calloc(buf_size, 1);
	*out_size = buf_size;
	memcpy(buffer, &head_begin, 4);		// place header begining
	memcpy(buffer + 4, ptr, 13);		// initial block (unchanged)
	switch (model){
	case GALILEO:				// only Mantis2 tested, bold assumption here
	case GALILEOTRIMIX:
	case ALADIN2G:
	case MERIDIAN:
	case CHROMIS:
	case MANTIS2:
	case ALADINSQUARE:
		memcpy(buffer + LIBDC_MAX_DEPTH, ptr + MANTIS_MAXDEPTH, 2);
		memcpy(buffer + LIBDC_DIVE_TIME, ptr + MANTIS_DIVE_TIME, 2);
		memcpy(buffer + LIBDC_MAX_TEMP, ptr + MANTIS_MAX_TEMP, 2);
		memcpy(buffer + LIBDC_MIN_TEMP, ptr + MANTIS_MIN_TEMP, 2);
		memcpy(buffer + LIBDC_SURF_TEMP, ptr + MANTIS_SURF_TEMP, 2);
		memcpy(buffer + LIBDC_GASMIX, ptr + MANTIS_GAS_MIX, 2);
		memcpy(buffer + LIBDC_TANK_PRESS, ptr + MANTIS_TANK_PRESS, 2);
		memcpy(buffer + LIBDC_SAMPLES_MANTIS, ptr + ASD_SAMPLES, max - ASD_SAMPLES);
		break;
	case G2:				// only G2 tested, bold assumption here too
	case G2HUD:
	case ALADINSPORTMATRIX:
	case ALADINA1:
		memcpy(buffer + LIBDC_MAX_DEPTH, ptr + G2_MAXDEPTH, 2);
		memcpy(buffer + LIBDC_DIVE_TIME, ptr + G2_DIVE_TIME, 2);
		memcpy(buffer + LIBDC_MAX_TEMP, ptr + G2_MAX_TEMP, 2);
		memcpy(buffer + LIBDC_MIN_TEMP, ptr + G2_MIN_TEMP, 2);
		memcpy(buffer + LIBDC_SURF_TEMP, ptr + G2_SURF_TEMP, 2);
		memcpy(buffer + LIBDC_SAMPLES_G2, ptr + ASD_SAMPLES, max - ASD_SAMPLES);
		break;
	default:
		fprintf(stderr, "Unsupported DC model 0x%2xd\n", model);
		free(buffer);
		buffer = NULL;
	}
	return(buffer);
}

/*
 * Return a utf-8 string from an .asd string.
 */
static char *asd_to_string(unsigned char *input, unsigned char **output)
{
	int size = input[3] * 2, j = 0;
	unsigned char *tmp = input;
	char *buffer = calloc (size + 1 , 1);

	*output = input + size + 4;
	if (size == 0)
		return NULL;
	if (!tmp)
		return NULL;
	tmp += 4;
	while (tmp < input + size + 4){
		unsigned char c = (tmp[1] << 4) + tmp[0];
		if (c <= 0x7F){
			buffer[j] = c;
		}
		else {
			buffer[j] = (c >> 6) | 0xC0;
			buffer[j + 1] = (c & 0x3F) | 0x80;
			j++;
		}
		j++;
		tmp += 2;
	}
	return buffer;
}

static void asd_build_dive_site(char *instring, char *coords, struct dive_site_table *sites, struct dive_site **asd_site)
{
	struct dive_site *site;
	double gpsX, gpsY;
	location_t gps_loc;

	sscanf(coords, "%lf %lf", &gpsX, &gpsY);
	gps_loc = create_location(gpsX, gpsY);
	site = get_dive_site_by_name(instring, sites);
	if (!site){
		if (!has_location(&gps_loc))
			site = create_dive_site(instring, sites);
		else
			site = create_dive_site_with_gps(instring, &gps_loc, sites);
	}
	*asd_site = site;
	free(instring);
	free(coords);
}




/*
 * Parse a dive in a mem buffer and return a pointer to next dive
 * or NULL if something goes wrong.
 */
unsigned char *asd_dive_parser(unsigned char *input, struct dive *asd_dive, struct dive_site_table *sites, unsigned char *maxptr)
{
	int dc_model, tmp = 0, rc = 0, size = 0;
	long dc_serial;
	unsigned char *ptr = input, *ptr1, *dc_data, end_seq[] = {0xff, 0xfe, 0xff};
	device_data_t *devdata = calloc(1, sizeof(device_data_t));
	asd_dive->dc.serial = calloc(64, 1);		// 64 bytes long seems more than suffice for a serial

	//input should point to dc model integer
	dc_model = (ptr[1] << 8) + ptr[0];
	if (ptr + 8 < maxptr)
		ptr += 8;
	else
		goto bailout;

	dc_serial = (ptr[3] << 24) + (ptr[2] << 16) + (ptr[1] << 8) + ptr[0];
	sprintf(asd_dive->dc.serial, "%ld", dc_serial);
	rc = prepare_data(dc_model, DC_FAMILY_UWATEC_MERIDIAN, devdata);
	asd_dive->dc.model = devdata->model;
	if (rc != DC_STATUS_SUCCESS)
		goto bailout;

	if (ptr + 4 < maxptr)
		ptr += 4;
	else
		goto bailout;

	//ptr should point to raw dc data.
	ptr1 = ptr;
	// move ptr to the end of dive computer data
	while (ptr + 3 < maxptr && memcmp(ptr, &end_seq, 3)){
		size++;
		ptr += 1;
	}
	dc_data = build_dc_data(dc_model, ptr1, size, &tmp);
	if (dc_data == NULL)
		goto bailout;				// freed in build_dc_data()

	rc = libdc_buffer_parser(asd_dive, devdata, dc_data, tmp);
	if (rc != DC_STATUS_SUCCESS)
		goto bailout;
	free(dc_data);					// allocated in build_dc_data()
	free(devdata);

	/*
	 * Now the non DC data fields.
	 * The string fields begin with a FF FE FF xx sequence, where xx is the size of the
	 * data, for two byte chars. Thus "Hello world" is 0x16 bytes long.
	 * There are no string fields merged with string ones, making things a bit more dificult.
         */
	char *d_locat = asd_to_string(ptr, &ptr);
	char *d_point = asd_to_string(ptr, &ptr);
	char *tmp_string = add_to_string_w_sep(d_locat, ", ", "%s", d_point);
	free(d_locat);
	free(d_point);
	char *d_coords = asd_to_string(ptr, &ptr);
	asd_build_dive_site(tmp_string, d_coords, sites, &asd_dive->dive_site);
	// next two bytes are the tank volume (mililiters) and two following are
	// unknown (always zero).
	get_cylinder(asd_dive, 0)->type.size.mliter = (ptr[1] << 8) + ptr[0];
	ptr += 4;
	get_cylinder(asd_dive, 0)->type.description = asd_to_string(ptr, &ptr);
	asd_dive->suit = asd_to_string(ptr, &ptr);
	// next two bytes are the weight in gr. Two following are zeroed.
	weightsystem_t ws = { { (ptr[1] << 8) + ptr[0] }, translate("gettextFromC", "unknown") };
	add_cloned_weightsystem(&asd_dive->weightsystems, ws);
	ptr += 4;
	taglist_add_tag(&asd_dive->tag_list, asd_to_string(ptr, &ptr));
	asd_to_string(ptr, &ptr);
	asd_to_string(ptr, &ptr);
	asd_to_string(ptr, &ptr);
	ptr += 6;  //*
	asd_dive->notes = asd_to_string(ptr, &ptr);
	return ptr;
bailout:
	free(devdata);
	free(asd_dive->dc.serial);
	return NULL;
}

/*
 * Main function
 */
int scubapro_asd_import(struct memblock *mem, struct dive_table *divetable, struct trip_table *trips, struct dive_site_table *sites)
{
	unsigned char *runner = mem->buffer,
		      *runner_max_val = mem->buffer + mem->size,
	               init_seq[] = {0x07, 0x00, 0x10, 0x00},
		       first_dive_seq[] = {0x74, 0x72, 0x79},
		       dive_seq[] = {0x80, 0x01};
	int dive_count = 0;

	setlocale(LC_NUMERIC, "POSIX");
	setlocale(LC_CTYPE, "");

	// check header
	if (memcmp(runner, &init_seq, 4)){
		fprintf(stderr, "This doesn't look like an .asd file. Please check it.\n");
		return 1;
	}
	// Jump to initial dive data secuence
	while (runner + 3 && memcmp(runner, &first_dive_seq, 3)){
		runner += 1;
	}
	runner += 3;
	if (runner >= runner_max_val){
		fprintf(stderr, ".asd file seems to be corrupt. Please check it.\n");
		return 1;
	}
	// We are on the first byte of the first dive (DC model). Subsequent dives in log
	// won't begin with 0x74 0x72 0x79 but with  0x80 0x01
	do {
		struct dive *asd_dive = alloc_dive();
		dive_count++;
		runner = asd_dive_parser(runner, asd_dive, sites, mem->buffer + mem->size);
		if (runner == NULL) {
			fprintf(stderr, "Error parsing dive %d\n", dive_count);
			free(asd_dive);
		} else {
			record_dive_to_table(asd_dive, divetable);
		}
		while (runner && memcmp(runner, &dive_seq, 2)){
			runner += 1;
		}
		runner += 2;
	} while (runner < runner_max_val);
	sort_dive_table(divetable);
	return 0;
}
