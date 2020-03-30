#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "gettext.h"

#include "dive.h"
#include "divelist.h"
#include "file.h"
#include "device.h"
#include "membuffer.h"
#include "subsurface-string.h"
#include "libdivecomputer.h"
#include "divesite.h"
#include "locale.h"
#include "errorhelper.h"

#define DEBUG

#define LT_TO_BYTE(_char) ((_char < 58) ? _char - 48: _char - 87)

#define S_FREE(_s)\
{\
	if(_s && *_s){\
		free(_s);\
		_s = NULL;\
	}\
}

/*
 * Extends functionality of add_to_string().
 * This one enables the user to set a separator string (commonly "\n" or ",")
 * and takes into account if orig string is null, in which case doesn't use the
 * separator string.
 * Returns a pointer to the joined string,
 * This function *does not* free the input string.
 */
char *add_to_string_w_sep(char *orig, const char *sep, const char *fmt, ...)
{
	char *str;
	va_list args;
	struct membuffer out = { 0 }, in = { 0 };

	va_start(args, fmt);
	put_vformat(&in, fmt, args);
	if (orig) {
		put_format(&out, "%s%s%s", orig, sep, mb_cstring(&in));
		str = copy_string(mb_cstring(&out));
	} else {
		str = copy_string(mb_cstring(&in));
	}
	va_end(args);

	free_buffer(&out);
	free_buffer(&in);

	return str;
}

/*
 * Returns a, \0 ended, sigle line string, or NULL on error.
 * May be an empty string.
 */
static char *get_single_line(char *beginptr)
{
	char *tmp, *eolptr;
	uint32_t s_size;

	eolptr = strchr(beginptr, 0x0A);
	if (eolptr == NULL)
		return NULL;
	s_size = eolptr - beginptr;
	if (s_size == 0)
		return NULL;
	tmp = calloc(s_size + 1, 1);
	tmp = memcpy(tmp, beginptr, s_size);
	return tmp;
}

/*
 * Returns a string with the name of the table of the input line.
 * May be null on error but not an empty string.
 */
static char *get_table_type(char *line)
{
	char *end, *tmp;
	int size;

	end = strstr(line, " VALUES");
	if (end == NULL)
		return NULL;
	size = end - (line + 12);
	if (size <= 0)
		return NULL;
	tmp = calloc(size + 1, 1);
	tmp = memcpy(tmp, line + 12, size);
	return tmp;
}

/*
 * LogTrak scapes single quotes with another quote.
 * Remove one of them if we find two in a string.
 * Input string must be freed by the caller.
 */
static char *lt_trim_quotes(const char *input)
{
	char *tmp, *tmp1, *runner;
	int size;

	if (input && *input){
		tmp = copy_string(input);
		runner = strstr(tmp, "\'\'");
		while (runner && *runner){
			size = runner - tmp;
			tmp1 = calloc(strlen(tmp), 1);
			tmp1 = memcpy(tmp1, tmp, size + 1);
			runner += 2;
			memcpy(tmp1 + size + 1, runner, strlen(runner));
			tmp = copy_string(tmp1);
			S_FREE(tmp1);
			runner = strstr(tmp, "\'\'");
		}
	} else {
		return NULL;
	}
	return(tmp);
}
/*
 * We can get some strings beginning and endig with "'", and some others simply "NULL".
 * The "'" must be removed to have a clean string, while the "NULL" means the string
 * is not set and ours' should simply point to NULL.
 * This only has to be used with non mandatory strings. Mandatory strings should be parsed
 * correctly while using sscanf().
 * For free edit strings (e.g. notes or dive points) we can get unicode chars writen as
 * plain ascii (e.g. "Hello\u000aWorld") for no printable chars or values greater than
 * 0x7f. Convert this ascii secuences into UTF-8.
 * Also take care of scapeg single quotes.
 */
static char *lt_process_string(char *input)
{
	char *tmp, *tmp1, *runner;

	if (!input)
		return NULL;
	// if input length is 4, is possible to find a NULL string
	if (strlen(input) == 4){
		if (!strcasecmp(input, "NULL")){
			return NULL;
		}
	}
	// trim init/end "'", if any
	if (input[0] == 0x27 && input[strlen(input) - 1] == 0x27){
		tmp = calloc(strlen(input) - 1, 1);
		tmp = memcpy(tmp, input + 1,strlen(input) - 2);
	} else {
		tmp = copy_string(input);
	}
	// search for no printable/unicode chars and convert them
	runner = tmp;
	while (runner && *runner){
		runner = strstr(runner, "\\u");
		if (runner){
			int size = runner - tmp;
			tmp1 = calloc(strlen(tmp) * 2 + 1, 1); // worst case, all unicode
			tmp1 = memcpy(tmp1, tmp, size);
			runner += 2;
			unsigned char c = (LT_TO_BYTE(runner[2]) << 4) + LT_TO_BYTE(runner[3]);
			if (c <= 0x7F){
				tmp1[size] = c;
			}
			else {
				tmp1[size] = (c >> 6) | 0xC0;
				tmp1[size + 1] = (c & 0x3F) | 0x80;
				size++;
			}
			runner += 4;
			memcpy(tmp1 + size + 1, runner, strlen(runner));
			tmp = copy_string(tmp1);
			runner = tmp;
			S_FREE(tmp1);
		}
	}
	// check if our string have scaped single quotes
	if (tmp && *tmp){
		tmp1 = lt_trim_quotes(tmp);
		tmp = copy_string(tmp1);
		S_FREE(tmp1);
	}
	return(tmp);
}

/*
 * LogTrak, like SmartTrak, stores the whole data downloaded from the DC.
 * It is stored like plain ascii sequence of chars (e.g. "a5a50eff..."); this
 * function process an string in such format  and returns a buffer with bytes.
 */
static bool lt_process_profile(char *input, unsigned char *output)
{
	char *runner = input;
	int i = 0;

	if (!runner || !*runner)
		return false;

	while (runner && *runner){
		int n, m;
		n = LT_TO_BYTE(runner[0]);
		m = LT_TO_BYTE(runner[1]);
		output[i] = ( n << 4 ) + m;
		i++;
		runner += 2;
	}
	return true;
}


/*
 * Get the entry of a table t_name refered by an id.
 * Only usable to get table entries with the id as first entry.
 * The resulting string is not a complete line. It begins where the meaning
 * data begin.
 */

static char *get_line_by_id(char* buffer, char *t_name, char *id)
{
	if (!t_name || !*t_name || !id || !*id)
		return NULL;

	char *str, *tmpstr1, *t_id;
	char *n_str = add_to_string_w_sep("INSERT INTO", " ", "%s", t_name);
	char *runner = strstr(buffer, n_str);

	while (runner && *runner){
		char *tmpstr = get_single_line(runner);
		/* break the loop if we can't get a line or empty, something went wrong */
		if (!tmpstr || !*tmpstr){
			S_FREE(tmpstr);
			break;
		}
		char *ttype = get_table_type(tmpstr);
		if (ttype && *ttype && !strcasecmp(ttype, t_name)){
			tmpstr1 = strchr(tmpstr, '(') + 1;
			sscanf(tmpstr1, "%m[0-9],", &t_id);
			if (!strcasecmp(t_id, id)){
				str = copy_string(tmpstr1);
				S_FREE(t_id);
				S_FREE(tmpstr);
				S_FREE(ttype);
				S_FREE(n_str);
				return str;
			}
			S_FREE(t_id);
		}
		S_FREE(tmpstr);
		S_FREE(ttype);
		runner = strchr(runner, 0x0A) + 1;
	}
	S_FREE(n_str);
	return NULL;
}

/*
 * Extract an ascii text string of unknown format from a logtrak db line.
 * String must be pointed to its first char.
 * Places the string on the passed variable ref and returns a pointer to the
 * following text to parse.
 */

static char *get_lt_string(char *input, char **output)
{
	char *tmp1 = input, *tmp2, *str;
	int size;

	tmp2 = strstr(tmp1, "',");
	if (!tmp2)
		tmp2 = strstr(tmp1, "')");
	size = tmp2 - tmp1;
	str = calloc(size + 1, 1);
	str = memcpy(str, tmp1, size);
	*output = str;
	return tmp2 + 2;
}

/*
 * Returns a dc_descriptor_t structure based on dc  model's number.
 * This ensures the model pased to libdc_buffer_parser() is a supported model and avoids
 * problems with shared model num devices by taking the family into account.  The family
 * is estimated elsewhere. AFAIK only DC_FAMILY_UWATEC_SMART is avaliable in libdc;
 * UWATEC_MERIDIAN kept just in case ...
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
/*
 * Fill the tank data of the passed dive structure.
 * In LogTrak, T_DIVE and T_BOTTLE are related via the T_EQUIPMENT id.
 * AFAIK there is no chance in LogTrak to manually add tanks, so there should
 * be no difference between DC data and divelog data, just the complementary
 * data (e.g. tank volume, tank material ...) not in DC.
 */
static void lt_get_tank_info(char *buffer, char *eq_id, struct dive *ltd)
{
	char *runner = buffer, *tankmat, *tankid, *eq_ref, *tmpstr1;
	int i = 0, tankvol, o2mix, startp, endp, hemix, tanknum;
	struct dive *d = ltd;

	runner = strstr(runner, "INSERT INTO T_BOTTLE");
	while (runner && *runner){
		char *tmpstr = get_single_line(runner);
		/* break the loop if we can't get a line or empty, something went wrong */
		if (!tmpstr || !*tmpstr){
			S_FREE(tmpstr);
			break;
		}
		char *ttype = get_table_type(tmpstr);
		if (ttype && *ttype && !strcasecmp(ttype, "T_BOTTLE")){
			tmpstr1 = strchr(tmpstr, '(') + 1;
			sscanf(tmpstr1, "%m[0-9],%d,%d,%d,%d,%m[a-zA-Z0-9-_'],%*d,'%*[0-9a-z]',%d,%*d,%d,%m[0-9],", &tankid, &tankvol, &o2mix, &startp, &endp, &tankmat, &hemix, &tanknum, &eq_ref);
			if (eq_ref && *eq_ref && !strcasecmp(eq_ref, eq_id)){
				/* Always prefer values from libdivecomputer */
				if (tankvol)
					get_cylinder(d, i)->type.size.mliter = tankvol;
				if (startp && !get_cylinder(d, i)->start.mbar)
					get_cylinder(d, i)->start.mbar = startp * 1000;
				if (endp && !get_cylinder(d, i)->end.mbar)
					get_cylinder(d, i)->end.mbar = endp * 1000;
				if (o2mix && !get_cylinder(d, i)->gasmix.o2.permille)
					get_cylinder(d, i)->gasmix.o2.permille = o2mix * 10;
				if (hemix && get_cylinder(d, i)->gasmix.he.permille)
					get_cylinder(d, i)->gasmix.he.permille = hemix * 10;
				i++;
			}
			S_FREE(tankmat);
			S_FREE(tankid);
			S_FREE(eq_ref);
		}
		S_FREE(tmpstr);
		S_FREE(ttype);
		runner = strchr(runner, 0x0A) + 1;
	}
	*ltd = *d;
}

/*
 * Build a buddies string for a given dive id.
 * Currently useless. The LogTrak version I have doesn't support buddies.
 * Even the demo divelog shipped, places a buddy in the notes.
 */
static void lt_build_buddies_info(char *buffer, char *ltd_id, struct dive *lt_dive)
{
	char *runner = buffer, *tmpstr1, *d_id, *p_id, *p_id2, *p_name, *p_surname;
	struct dive *d = lt_dive;

	runner = strstr(runner, "INSERT INTO T_DIVE_PARTNERS");
	while (runner && *runner){
		char *tmpstr = get_line_by_id(runner, "T_DIVE_PARTNERS", ltd_id);
		if (tmpstr && *tmpstr){
			runner = strstr(runner, ltd_id);
			sscanf(tmpstr, "%m[0-9],%m[0-9],", &d_id, &p_id);
			if (d_id && *d_id && !strcasecmp(ltd_id, d_id)){
				char *runner_1 = buffer;
				tmpstr1 = get_line_by_id(runner_1, "T_PARTNER", p_id);
				if (tmpstr1 && *tmpstr1){
					sscanf(tmpstr1, "%m[0-9],", &p_id2);
					if (p_id2 && *p_id2 && !strcasecmp(p_id, p_id2)){
						char *tmpstr2;
						char *full_name = calloc(1, 1);
						tmpstr2 = strstr(tmpstr1, ",'") + 2;
						tmpstr2 = get_lt_string(tmpstr2, &p_name) + 1;
						tmpstr2 = get_lt_string(tmpstr2, &p_surname);
						p_name = lt_process_string(p_name);
						p_surname = lt_process_string(p_surname);
						if (p_name && *p_name)
							full_name = copy_string(p_name);
						if (p_surname && *p_surname)
							full_name = add_to_string_w_sep(full_name, " ", "%s", p_surname);
						if (full_name && *full_name)
							d->buddy = add_to_string_w_sep(d->buddy, ",", "%s", full_name);
						free(full_name);
						S_FREE(p_name);
						S_FREE(p_surname);
						S_FREE(p_id2);
					}
					S_FREE(tmpstr1);
				}
				S_FREE(d_id);
			}
			S_FREE(p_id);
			S_FREE(tmpstr);
		}
		runner = strchr(runner, 0x0A) + 1;
	}
	*lt_dive = *d;
}


/*
 * Build a site for a given dive id.
 * Check if it exist, to avoid duplicities.
 * Like T_DIVE, this contains notes, so it's not fully parseable with sscanf
 */
static void lt_build_dive_site(char *buffer, char *site_id, struct dive_site **lt_site)
{
	char *tmpstr1, *tmpstr2, *tmpstr3, *s_name, *s_comment, *s_loc_id, *l_name, *built_name;
	struct dive_site *site;
	location_t gps_loc;
	double gpsX, gpsY;

	tmpstr1 = get_line_by_id(buffer, "T_SITE", site_id);
	tmpstr2 = strstr(tmpstr1,",'") + 2;
	tmpstr3 = get_lt_string(tmpstr2, &s_name);
	s_name = lt_process_string(s_name);
	sscanf(tmpstr3, "%lf,%lf,%*d,", &gpsX, &gpsY);

	/* get comment by hand, if any */
	tmpstr2 = strstr(tmpstr3, ",'") + 2;
	tmpstr3 = get_lt_string(tmpstr2,&s_comment);
	s_comment = lt_process_string(s_comment);
	sscanf(tmpstr3,"%*m[a-zA-Z0-9],%m[0-9])", &s_loc_id);
	S_FREE(tmpstr1);

	/* get location name */
	tmpstr1 = get_line_by_id(buffer, "T_LOCATION", s_loc_id);
	S_FREE(s_loc_id);
	tmpstr2 = strstr(tmpstr1, ",'") + 2;
	tmpstr3 = get_lt_string(tmpstr2, &l_name);
	l_name = lt_process_string(l_name);
	S_FREE(tmpstr1);

	/* build a complete name */
	if (s_name && *s_name)
		built_name = add_to_string_w_sep(l_name, ",  ", "%s", s_name);
	else
		built_name = copy_string(l_name);
	S_FREE(l_name);
	S_FREE(s_name);
	/* build gps position */
	gps_loc = create_location(gpsX, gpsY);

	/* build the dive site structure */
	site = get_dive_site_by_name(built_name, &dive_site_table);
	if (!site){
		if (!has_location(&gps_loc))
			site = create_dive_site(built_name, &dive_site_table);
		else
			site = create_dive_site_with_gps(built_name, &gps_loc, &dive_site_table);
	}
	S_FREE(built_name);
	if (s_comment && *s_comment){
		site->notes = copy_string(s_comment);
		free(s_comment);
	}

	*lt_site = site;
}

/*
 * Main function.
 * Runs along tdive_list importing its data to subsurface's dives.
 * WARNING!! LogTrak supports more than a divelog in a single db, so we may end up
 * with a mixed divelog.
 * Input: a memblock buffer produced by readfile() an a dive_table list.
 * Output: Luckily adds LogTrak dives to the dive_table list.
 * Returns: Integer (0 as default or negative values on error).
 */
int logtrak_import(struct memblock *mem, struct dive_table *table)
{
	char *runner=(char *)mem->buffer, *instring=NULL, *strend, *tmpstr=NULL, *ttype=NULL, *t_str, *p_suit;
	char *ltd_id, *ltd_type, *ltd_temp, *ltd_weather, *ltd_viz, *ltd_vote, *ltd_notes, *ltd_ref_eq, *ltd_ref_site, *ltd_dc, *ltd_dc_id, *ltd_dive, *ltd_max_depth, *ltd_mintemp, *ltd_airtemp, *ltd_dc_soft, *ltd_gf_low, *ltd_gf_high, *ltd_log_id, *db_ver;

	/* Manually parse the T_DIVE entries, filling on the fly as much as possible of a ssrf dive */
	setlocale(LC_NUMERIC, "POSIX");
	setlocale(LC_CTYPE, "");
	runner = strstr(runner, "INSERT INTO T_DIVE");
	while (runner && *runner){
		device_data_t *devdata = calloc(1, sizeof(device_data_t));
		dc_family_t dc_fam = DC_FAMILY_NULL;
		struct dive *lt_dive = alloc_dive();
		float p_weight = 0;
		int dc_model = 0;

		tmpstr=get_single_line(runner);
		/* break the loop if we can't get a line or empty, something went wrong */
		if (!tmpstr || !*tmpstr){
			report_error("[LogTrak import] Couldn't get any dive. Check the selected file.");
			free(devdata);
			free(lt_dive);
			S_FREE(tmpstr);
			break;
		}
		ttype=get_table_type(tmpstr);
		if (ttype && *ttype && !strcasecmp(ttype, "T_DIVE")){
			instring = strchr(tmpstr, '(') + 1;
			// Read up to time zone. Discard this one.
			sscanf(instring, "%m[0-9],'%m[a-zA-Z0-9]',%m[0-9],'%m[a-zA-Z0-9]',%m[0-9],%m[0-9],]",\
				    &ltd_id, &ltd_type, &ltd_temp, &ltd_weather, &ltd_viz, &ltd_vote);

			if (ltd_temp && *ltd_temp)
				lt_dive->watertemp.mkelvin = C_to_mkelvin(lrint(strtod(ltd_temp, NULL)));
			S_FREE(ltd_temp);
			if (ltd_weather && *ltd_weather)
				taglist_add_tag(&lt_dive->tag_list, ltd_weather);
			S_FREE(ltd_weather);
			if (ltd_viz && *ltd_viz)
				lt_dive->visibility = atoi(ltd_viz);
			S_FREE(ltd_viz);
			if (ltd_vote && *ltd_vote)
				lt_dive->rating = atoi(ltd_vote);
			S_FREE(ltd_vote);

			/* Move past time zone */
			for (int i = 0; i < 3; i++)
				instring = strstr(instring, "',") + 3;
			/*
			 * The notes string will be manually parsed as it can contain every printable caracter
			 * and even non printable in unicode format; e.g. "\n" will show as \u000a. The notes
			 * string is 256 char long, at most.
			 */
			strend = strstr(instring, "',");
			ltd_notes = calloc(strend - instring + 1, 1);
			ltd_notes = memcpy(ltd_notes, instring, strend - instring);
			lt_dive->notes = lt_process_string(ltd_notes);
			S_FREE(ltd_notes);

			// Continue with sscanf. Send useless trends to devnull
			instring = strend + 2;
			sscanf(instring,"%m[0-9A-Z],%m[0-9A-Z],%*m[0-9A-Z],%m[0-9A-Z],%m[0-9A-Z],'%m[0-9A-Za-z]',%*m[0-9A-Z],%*m[0-9A-Z],%*m[0-9A-Z],%m[0-9A-Z],%*m[0-9A-Z],%m[0-9A-Z],%*m[0-9A-Z],%*m[0-9A-Z],%*m[0-9A-Z],%*m[0-9A-Z],%*m[0-9A-Z],%*m[0-9A-Z],%*m[0-9A-Z],%*m[0-9A-Z],'%*m[0-9A-Za-z]','%*m[0-9A-Za-z]','%*m[0-9A-Za-z]',%*m[0-9A-Z],%*m[0-9A-Z],%*m[0-9A-Z],'%*m[0-9A-Za-z]','%*m[0-9A-Za-z]',%*m[0-9A-Z],'%*m[0-9A-Za-z]',%*m[0-9A-Z],%*m[0-9A-Z],%m[0-9A-Z],%*m[0-9A-Z],%*m[0-9A-Z],%*m[0-9A-Z],%*m[0-9A-Z],%*m[0-9A-Z],%*m[0-9A-Z],%*m[0-9A-Z],'%*m[0-9A-Za-z]',%*m[0-9A-Z],%*m[0-9A-Z],%*m[0-9A-Z],%*m[0-9A-Z],%*m[0-9A-Z],%*m[0-9A-Z],%*m[0-9A-Z],'%*m[0-9A-Za-z]','%*m[0-9A-Za-z]','%*m[0-9A-Za-z]',%m[0-9A-Z],%*m[0-9A-Z],%*m[0-9A-Z],%*m[0-9A-Z],%m[0-9A-Z],%m[0-9A-Z],%m[0-9A-Z]",&ltd_ref_eq, &ltd_ref_site, &ltd_dc, &ltd_dc_id, &ltd_dive, &ltd_max_depth, &ltd_mintemp, &ltd_airtemp, &ltd_dc_soft, &ltd_gf_low, &ltd_gf_high, &ltd_log_id);

			/* Process DC data */
			int prf_size = ceil(strlen(ltd_dive) / 2);
			unsigned char prf_buffer[prf_size];
			if (!lt_process_profile(ltd_dive, prf_buffer))
				fprintf(stderr, "--->[lt_process_profile] FAILED for string: %s\n", ltd_dive);
			S_FREE(ltd_dive);
			if (ltd_dc && *ltd_dc)
				dc_model = lrint(strtod(ltd_dc, NULL)) & 0xFF;
			S_FREE(ltd_dc);
			dc_fam = DC_FAMILY_UWATEC_SMART;
			devdata->descriptor = get_data_descriptor(dc_model, dc_fam);
			if (devdata->descriptor){
				devdata->vendor = dc_descriptor_get_vendor(devdata->descriptor);
				devdata->product = dc_descriptor_get_product(devdata->descriptor);
				/* No need to check vendor or product if we got a correct descriptor */
				devdata->model = add_to_string_w_sep(NULL, NULL, "%s %s", devdata->vendor, devdata->product);
				lt_dive->dc.model = devdata->model;
//debug
	for (int i = 0; i < prf_size; i++){
		fprintf(stderr, "%2X ", prf_buffer[i]);
	}
	fprintf(stderr, "\n");
//debug
				libdc_buffer_parser(lt_dive, devdata, prf_buffer, prf_size);
				set_dc_deviceid(&lt_dive->dc, strtoul(lt_process_string(ltd_dc_id), NULL, 10));
				lt_dive->dc.serial = copy_string(ltd_dc_id);
				lt_dive->dc.fw_version = copy_string(lt_process_string(ltd_dc_soft));
				create_device_node(lt_dive->dc.model, lt_dive->dc.deviceid, lt_dive->dc.serial, lt_dive->dc.fw_version, lt_dive->dc.model);
			} else {
				report_error("Unsuported DC model 0x%X\n", dc_model);
			}
			S_FREE(ltd_dc_id);
			S_FREE(ltd_dc_soft);
			free(devdata);

			/* Get some DC related data, but always prefer libdc processed data */
			if (lt_dive->maxdepth.mm == 0 && ltd_max_depth && strcmp(ltd_max_depth, "0"))
				lt_dive->maxdepth.mm = lrint(strtod(ltd_max_depth, NULL) * 10);
			S_FREE(ltd_max_depth);
			if (lt_dive->mintemp.mkelvin == 0 && ltd_mintemp && strcmp(ltd_mintemp, "0"))
				lt_dive->mintemp.mkelvin = C_to_mkelvin(strtod(ltd_mintemp, NULL) / 10);
			S_FREE(ltd_mintemp);
			if (lt_dive->airtemp.mkelvin == 0 && ltd_airtemp && strcmp(ltd_airtemp, "0"))
				lt_dive->airtemp.mkelvin = C_to_mkelvin(strtod(ltd_airtemp, NULL) / 10);
			S_FREE(ltd_airtemp);

			/* Get suit and weight */
			t_str = get_line_by_id((char *)mem->buffer, "T_EQUIPMENT", ltd_ref_eq);
			if (t_str && *t_str){
				sscanf(t_str, "%*m[0-9],%*m[0-9a-zA-Z'],%m[0-9a-zA-Z'],%f", &p_suit, &p_weight);
				free(t_str);
			}
			lt_dive->suit = lt_process_string(p_suit);
			S_FREE(p_suit);
			weightsystem_t ws = { { lroundf(p_weight * 1000) }, translate("gettextFromC", "unknown") };
			add_cloned_weightsystem(&lt_dive->weightsystems, ws);

			/* Get tanks info. Tanks are refered to dive via the T_EQUIPMENT id */
			lt_get_tank_info((char *)mem->buffer, ltd_ref_eq, lt_dive);
			S_FREE(ltd_ref_eq);

			/* Get buddies info. Refered via T_DIVE_PARTNERS and T_PARTNER */
			lt_build_buddies_info((char *)mem->buffer, ltd_id, lt_dive);
			S_FREE(ltd_id);

			/* Get site/location info. Refered via T_SITE */
			lt_build_dive_site((char *)mem->buffer, ltd_ref_site, &lt_dive->dive_site);
			S_FREE(ltd_ref_site);

			/* Set some extra data which can be interesting */
			char *tmpstr_1 = strstr(get_line_by_id((char *)mem->buffer, "TABLE_DBVERSION", "0"), ",'") + 2;
			get_lt_string(tmpstr_1, &db_ver);
			lt_process_string(db_ver);
			add_extra_data(&lt_dive->dc, "LogTrak Version", db_ver);
			S_FREE(db_ver);
			if (ltd_type && *ltd_type)
				add_extra_data(&lt_dive->dc, "DC Type", ltd_type);
			S_FREE(ltd_type);
			if (ltd_dc_soft && *ltd_dc_soft)
				add_extra_data(&lt_dive->dc, "DC Firmware Version", ltd_dc_soft);
			S_FREE(ltd_dc_soft);
			ltd_gf_low = lt_process_string(ltd_gf_low);
			ltd_gf_high = lt_process_string(ltd_gf_high);
			if (ltd_gf_low && strcmp(ltd_gf_low, "0"))
				add_extra_data(&lt_dive->dc, "GF Low", ltd_gf_low);
			S_FREE(ltd_gf_low);
			if (ltd_gf_high && strcmp(ltd_gf_high, "0"))
				add_extra_data(&lt_dive->dc, "GF High", ltd_gf_high);
			S_FREE(ltd_gf_high);
			add_extra_data(&lt_dive->dc, "DC Serial", lt_dive->dc.serial);

			record_dive_to_table(lt_dive, table);
			sort_dive_table(table);
		}
		runner=runner+strlen(tmpstr)+1;
		free(ttype);
		free(tmpstr);
		ttype = tmpstr = NULL;
	}
	return 0;
}
