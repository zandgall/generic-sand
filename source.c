#include <stdio.h>
#ifdef _WIN32
#include <SDL.h>
#else
#include <SDL2/SDL.h>
#endif
#include <stdbool.h>
#include <time.h>
#include <stdlib.h>
#include <regex.h>
#include <dirent.h>
#include <string.h>
#undef main

SDL_Surface* surf;
SDL_Surface* window_surface;

bool mouseLeft;
int mouseX, mouseY;
int relX, relY;

#define WIDTH 80
#define HEIGHT 80
#define WINDOW_SCALE 8
#define ITERATIONS 4
#define STEPPING 2
#define MAX_RULES 256

#define COL(r,g,b) ((uint32_t)(r<<16)+(uint32_t)(g<<8)+(uint32_t)(b)+(uint32_t)(255	<<24))

#define AIR COL(155, 215, 232)

struct identity {
	const char* name;
	uint32_t member_count;
	uint32_t* members; // Elements of an identity, say, "solid"
}* identities;
int n_identities;

struct match_t {
	// -1 - match anything, 0 - exact color, 1 - identity
	int8_t type;
	uint32_t value;
	// char* identity;
};

bool matchCmp(const struct match_t a, const struct match_t b) {
	if(a.type!=b.type)
		return false;
	if(a.type==-1)
		return true;
	return a.value == b.value;
}

struct region {
	uint32_t* unique_members;	
	uint32_t num_unique_members;
} *quadrants, *quad_quadrants;

struct replace_t {
	int8_t type;
	int8_t refX, refY;
	uint32_t value;
	// const char* identity;
	// type = -1 -> do not replace
	// type = 0 -> replace with color value of 'value'
	// type = 1 -> replace with color of the pixel that is (refX, refY) away from the current position. if a referenced cell is out of bounds, it will return AIR
	// type = 2 -> replace with color of the pixel referenced by (refX, refY) as seen above, with changes to the red, green, and blue of the color as laid out by 'value'
	//					which, in this case, will be composed of 4 signed 'int8_t's, value = 0x XX'XX'XX'XX, each XX->signed 8 bit int
	// type = 3 -> replace with a random entry in the given identity
};

struct rule {
	// match_t is a type that will be used to determine whether a color matches a rule or not
	struct match_t match[5][5];
	// the "search_for" contains all unique match_t's, to see if a region has all the elements used in the match block
	struct match_t* search_for;
	int num_search_for;
	// replace_t is a type that will be used to determine how each cell confined by the rule should be changed
	struct replace_t replace[5][5];
	
	// Number between 0 and 1 that determines randomly if the rule will proceed, or just fail
	float chance;
}* rules;
int n_rules;

uint32_t binds[255];

unsigned int color(uint8_t r, uint8_t g, uint8_t b) {
	return (uint32_t)(r << 16) + (uint32_t)(g << 8) + (uint32_t)(b) + (uint32_t)(255 << 24);
}

void put(uint32_t col, int x, int y) {
	*((uint32_t*)(surf->pixels + y * surf->pitch + x * surf->format->BytesPerPixel)) = col;
}

uint32_t get(int x, int y) {
	if(x < 0 || x >= WIDTH || y < 0 || y >= HEIGHT)
		return AIR;
	return *((uint32_t*)(surf->pixels + y * surf->pitch + x * surf->format->BytesPerPixel));
}

int getIdentity(const char* identity_name) {
	if(identity_name == NULL)
		return -1;
	for(int i = 0; i < n_identities; i++)
		if(identities[i].name != NULL && strcmp(identities[i].name, identity_name)==0)
			return i;
	return -1;
}

bool isIdentity(const uint32_t identity_index, uint32_t element) {
	if(identity_index < 0 || identity_index >= n_identities)
		return false;
	struct identity* id = identities + identity_index;
	for(int i = 0; i < id->member_count; i++)
		if(element == id->members[i])
			return true;
	
	return false;
	
}

bool regionHas(struct region *r, struct match_t t) {
	if(r == NULL)
		return false;
	if(t.type==-1)
		return true;
	if(t.type==0) {
		for(int i = 0; i < r->num_unique_members; i++)
			if(t.value==r->unique_members[i])
				return true;
	} else
		for(int i = 0; i < r->num_unique_members; i++)
			if(isIdentity(t.value, r->unique_members[i]))
				return true;
	return false;
}

bool quadrantHas(int x, int y, struct match_t t) {
	if(x < 0 || x >= 2 || y < 0 || y >= 2)
		return false;
	if(t.type == -1)
		return true;
	struct region *r = quadrants + x + y*2;
	if(t.type==0) {
		for(int i = 0; i < r->num_unique_members; i++)
			if(t.value==r->unique_members[i])
				return true;
	} else
		for(int i = 0; i < r->num_unique_members; i++)
			if(isIdentity(t.value, r->unique_members[i]))
				return true;
	return false;
}

bool quadquadrantHas(int x, int y, struct match_t t) {
	if(x < 0 || x >= 4 || y < 0 || y >= 4)
		return false;
	if(t.type == -1)
		return true;
	struct region *r = quad_quadrants + x + y*4;
	if(t.type==0) {
		for(int i = 0; i < r->num_unique_members; i++)
			if(t.value==r->unique_members[i])
				return true;
	} else
		for(int i = 0; i < r->num_unique_members; i++)
			if(isIdentity(t.value, r->unique_members[i]))
				return true;
	return false;
}

bool potential(struct rule rule, int x, int y) {
	if(x >= WIDTH || x + 5 < 0 || y >= HEIGHT || y + 5 < 0)
		return false;

	/*struct region *tl = NULL, *tr = NULL, *bl = NULL, *br = NULL;
	struct region *qtl = NULL, *qtr = NULL, *qbl = NULL, *qbr = NULL;
	if(y >= 0) {
		if(x >= 0) {
			tl = quadrants + (2*x / WIDTH) + 2*(2*y/HEIGHT);
			qtl = quad_quadrants + (4*x / WIDTH) + 4(4*y / HEIGHT);
		}
		if(x + 5 < WIDTH) {
			if(floor((2.f*x+10)/WIDTH) != floor((2.f*x)/WIDTH)) {
				tr = quadrants + (2*x+10) / WIDTH + 2*(2*y/HEIGHT);
				qtr = quad_quadrants + (4*x+20) / WIDTH + 4(4*y / HEIGHT);
			} else if (floor((4.f*x+20)/WIDTH) != floor((4.f*x)/WIDTH))
				qtr = quad_quadrants + (4*x+20) / WIDTH + 4(4*y / HEIGHT);
		}
	}
	if(y + 5 < HEIGHT && floor((2.f*y+10)/HEIGHT) != floor((2.f*y)/HEIGHT)) {
		if(x >= 0)
			bl = &quadrants[(2*x / WIDTH) + 2*((2*y+10)/HEIGHT)];
		if(x + 5 < WIDTH && floor((2.f*x+10)/WIDTH) != floor((2.f*x)/WIDTH))
			br = &quadrants[((2*x+10) / WIDTH) + 2*((2*y+10)/HEIGHT)];
	}
	for(int i = 0; i < rule.num_search_for; i++)
		if(!regionHas(tl, rule.search_for[i]) && !regionHas(tr, rule.search_for[i])
			&& !regionHas(bl, rule.search_for[i]) && !regionHas(br, rule.search_for[i]))
			return false;
	return true;*/

	int ql = floor((2.f*x)/WIDTH), qt = floor((2.f*y)/HEIGHT), 
		qr = floor((2.f*x+10)/WIDTH), qb=floor((2.f*y+10)/HEIGHT);

	int qql = floor((4.f*x)/WIDTH), qqt = floor((4.f*y)/HEIGHT), 
		qqr = floor((4.f*x+20)/WIDTH), qqb=floor((4.f*y+20)/HEIGHT);

	for(int i = 0; i < rule.num_search_for; i++) {
		if(ql >= 0) {
			if(qt >= 0 && quadrantHas(ql, qt, rule.search_for[i]))
				continue;
			if(qb < HEIGHT && qb!=qt && quadrantHas(ql, qb, rule.search_for[i]))
				continue;
		}
		if(qr < WIDTH && qr!=ql) {
			if(qt >= 0 && quadrantHas(qr, qt, rule.search_for[i]))
				continue;
			if(qb < HEIGHT && qb!=qt && quadrantHas(qr, qb, rule.search_for[i]))
				continue;
		}

		if(qql >= 0) {
			if(qqt >= 0 && quadquadrantHas(qql, qqt, rule.search_for[i]))
				continue;
			if(qqb < HEIGHT && qqb!=qqt && quadrantHas(qql, qqb, rule.search_for[i]))
				continue;
		}
		if(qqr < WIDTH && qqr!=qql) {
			if(qqt >= 0 && quadrantHas(qqr, qqt, rule.search_for[i]))
				continue;
			if(qqb < HEIGHT && qqb!=qqt && quadrantHas(qqr, qqb, rule.search_for[i]))
				continue;
		}

		return false;
	}
	return true;

}

bool matches(struct rule rule, int x, int y) {
	if((double)rand()/RAND_MAX > rule.chance)
		return false;
	if(!potential(rule, x, y))
		return false;
	for(int j = 0; j < 5; j++)
		for(int i = 0; i < 5; i++) {
			if(rule.match[j][i].type==-1)
				continue; // Wildcard, matches anything including border
			if(i+x < 0 || i + x >= WIDTH || j + y < 0 || j + y >= HEIGHT)// Is not wildcard and border/out of bounds
				return false;
			if(rule.match[j][i].type==0 && rule.match[j][i].value != get(i+x, j+y))
				return false;
			if(rule.match[j][i].type==1 && !isIdentity(rule.match[j][i].value, get(i+x, j+y)))
				return false;
		}
	return true;
}

void enforce(struct rule rule, int x, int y) {
	uint32_t source[5][5];
	
	for(int j = 0; j < 5; j++)
		for(int i = 0; j+y >=0 && j+y < HEIGHT && i < 5; i++) {
			if(rule.replace[j][i].type == -1 || i + x < 0 || i + x >= WIDTH)
				continue;
			if(rule.replace[j][i].type == 0) // Set as color
				source[j][i] = rule.replace[j][i].value;
			if(rule.replace[j][i].type >= 1) // Set to referenced pixel
				source[j][i] = get(i+x+rule.replace[j][i].refX, j+y+rule.replace[j][i].refY);
			if(rule.replace[j][i].type == 2) // Set to edited referenced pixel
				source[j][i] = 	  ((((source[j][i]>>16)&0xff)+(int8_t)((rule.replace[j][i].value>>16)&0xff)) << 16)
								+ ((((source[j][i]>> 8)&0xff)+(int8_t)((rule.replace[j][i].value>> 8)&0xff)) <<  8)
								+ (source[j][i]&0xff)+(int8_t)(rule.replace[j][i].value&0xff)
								+ (source[j][i]&0xff000000);
			if(rule.replace[j][i].type == 3) {
				struct identity* id = identities + rule.replace[j][i].value;
				if(id == NULL)
					source[j][i] = get(i + x, j + y);
				else
					source[j][i] = id->members[rand() % id->member_count];
			}
		}
	
	
	for(int j = 0; j < 5; j++)
		for(int i = 0; j+y >=0 && j+y < HEIGHT && i < 5; i++) {
			if(rule.replace[j][i].type == -1 || i + x < 0 || i + x >= WIDTH)
				continue;
			put(source[j][i], i+x, j+y);
		}
}

char* reggrab(const char* string, regmatch_t match) {
	char* out = (char*)malloc(match.rm_eo - match.rm_so + 1);
	out[match.rm_eo - match.rm_so] = '\0';
	strncpy(out, string + match.rm_so, match.rm_eo - match.rm_so);
	return out;
}

bool isUIntMemberOf(uint32_t val, uint32_t* list, uint32_t max) {
	for(int i = 0; i < max; i++)
		if(list[i]==val)
			return true;
	return false;
}

bool isMatchMemberOf(struct match_t val, struct match_t* list, uint32_t max) {
	for(int i = 0; i < max; i++)
		if(matchCmp(list[i],val))
			return true;
	return false;
}

void loadRule(const char* filepath) {
	FILE *f = fopen(filepath, "r");
		
	const char* c_red = "\033[0;31m";
	const char* c_def = "\033[0m";
	int line_number = 0;
	if(f==NULL) {
		printf("%s%s - Couldn't create rule: File not found!%s\n", c_red, filepath, c_def);
		return;
	}
	
	int ret = 0;
	regex_t whitespace, term, element, element_identity, color, identity, reference, edit, color_debug, is_symbol_def, y_mirror, x_mirror, chance;
	ret = regcomp(&whitespace, "^\\s*?$", REG_EXTENDED); // Empty line
	ret = regcomp(&element, "^#(\\S{6}):\\s+(\\S)?", REG_EXTENDED); // #element: (keybind)
	ret = regcomp(&element_identity, "\\s*-\\s*\"(.*?)\"", REG_EXTENDED);
	ret = regcomp(&term, "\\s*?(\\(\\s*?(\\S+)\\s*?,\\s*?(\\S+)\\s*?\\)|\\(.*?,.*?,.*?,.*?,.*?\\)|#\\S+|\".*?\"|\\S)(\\s+|$)", REG_EXTENDED); // any selectable element surrounded by whitespace
	ret = regcomp(&color, "\\s*?#(\\S\\S\\S\\S\\S\\S)(\\s+|$)", REG_EXTENDED); // exactly 6 non-whitespace characters following '#'
	ret = regcomp(&color_debug, "\\s*?#(\\S*?)(\\s+|$)", REG_EXTENDED); // non-whitespace characters following '#', used to tell the user that they were close to COLOR
	ret = regcomp(&identity, "\\s*?\"(.*)\"\\s*?", REG_EXTENDED); // one or more non-whitespace characters in quotes
	ret = regcomp(&reference, "\\s*?\\(\\s*?(\\S+)\\s*?,\\s*?(\\S+)\\s*?\\)\\s*?", REG_EXTENDED); // 2 non-whitespace strings of characters in between ( ) and separated by a comma
	ret = regcomp(&edit, "\\s*?\\(\\s*?(\\S+)\\s*?,\\s*?(\\S+)\\s*?,\\s*?(\\S+)\\s*?,\\s*?(\\S+)\\s*?,\\s*?(\\S+)\\s*?\\)\\s*?", REG_EXTENDED); // 5 non-whitespace strings of characters between ( ) and separated by commas
	ret = regcomp(&is_symbol_def, "^\\S\\s*?=>\\s*?(.+)$", REG_EXTENDED); // single non-whitespace character => list of non-whitespace characters
	ret = regcomp(&y_mirror, "y", REG_EXTENDED);
	ret = regcomp(&x_mirror, "x", REG_EXTENDED);
	ret = regcomp(&chance, "([0-9|.]+)%", REG_EXTENDED);

	char symbols[255] = {0};
	int n_symbols = 0;
	char* resolved[255];
	
	char line[255];
	while(fgets(line, 255, f)) {
		line_number++;
		if(regexec(&whitespace, line, 0, NULL, 0)==0) // empty line
			continue;
		if((ret=strncmp(line, "rule:", 5))==0) {
			int rule_line_number = line_number;
			struct match_t* unique_members = (struct match_t*)malloc(sizeof(struct match_t)*5*5);
			struct rule rule;
			rule.num_search_for = 0;
			rule.chance = 1;
			for(int i = 0; i < 5; i++) {
				line_number++;
				char rule_str[255];
				if(!fgets(rule_str, 255, f)) {
					printf("%s%s - Couldn't create rule: End of file before end of rule, at line #%d%s\n", c_red, filepath, line_number, c_def);			
					return;
				}
				for(int j = 0, offset = 0; j < 10; j++) {
					regmatch_t regmatch[2];
					regexec(&term, rule_str + offset, 2, regmatch, 0);
					if(j==5 && i == 2) {
						offset += regmatch[1].rm_eo;
						regexec(&term, rule_str + offset, 2, regmatch, 0);
					}
					if(regmatch[1].rm_so == -1 || (regmatch[1].rm_eo == -1 && j < 9)) {
						printf("%s%s - Couldn't create rule: Not enough terms in rule's row #%d, line #%d%s (Need 10 terms total, 5 match + 5 replace!)\n", c_red, filepath, line_number-rule_line_number, line_number, c_def);
						return;
					}
					// ret = regexec(&reference, )
					char* term = NULL, needToFree = 0;
					if(regmatch[1].rm_eo - regmatch[1].rm_so == 1 && rule_str[regmatch[1].rm_so + offset]!='*') {
						for(int s = 0; s < n_symbols && term==NULL; s++)
							if(rule_str[regmatch[1].rm_so+offset]==symbols[s]) {
								term = resolved[s];
								break;
							}
						if (term == NULL)
							printf("%s%s - Couldn't create rule: Unknown symbol '%c', line #%d%s\n", c_red, filepath, rule_str[regmatch[1].rm_so + offset], line_number, c_def);
					} else {
						needToFree = 1;
						term = reggrab(rule_str + offset, regmatch[1]);
					}
					if(j < 5) { // In match block
						regmatch_t value[2];
						if(term[0] == '*')
							rule.match[i][j].type = -1;
						else if (regexec(&color, term, 2, value, 0) == 0) {
							rule.match[i][j].type = 0;
							char num_string[7];
							strncpy(num_string, term + value[1].rm_so, 6);
							rule.match[i][j].value = (uint32_t)strtol(num_string, NULL, 16) + (uint32_t)(255<<24);
						} else if (regexec(&identity, term, 2, value, 0) == 0) {
							rule.match[i][j].type = 1;
							char* identity_name = reggrab(term, value[1]);
							rule.match[i][j].value = getIdentity(identity_name);
							if(rule.match[i][j].value==-1) {
								rule.match[i][j].value = n_identities;
								identities = realloc(identities, sizeof(struct identity)*(n_identities+1));
								identities[n_identities].member_count = 0;
								identities[n_identities].members = NULL;
								identities[n_identities].name = identity_name; // No need to strcpy 'i'
								n_identities++;
								// printf("%s%s - Couldn't create rule: Unknown identity \"%s\" referenced in match! Line #%d%s\n", c_red, filepath, identity_name, line_number, c_def);
							}
						} else {
							if (regexec(&color_debug, term, 2, value, 0) == 0)
								printf("%s%s - Couldn't create rule: It looks like you intended to create a color identity here, colors are are written as \"#\" followed by exactly and only 6 hex characters. Line #%d%s\n", c_red, filepath, line_number, c_def);
							else
								printf("%s%s - Couldn't create rule: Unknown matching term syntax, match term #%d, rule row #%d, Line #%d%s\n", c_red, filepath, j, i, line_number, c_def);
							
							while(i < 5) { // Speed through rest of rule, ignoring contents
								i++;
								if(fgets(rule_str, 255, f)); // prevent unused result warning
								line_number++;
							}
							j = 10;
							break;
						}
						if(!isMatchMemberOf(rule.match[i][j], unique_members, rule.num_search_for))
							unique_members[rule.num_search_for++]=rule.match[i][j];
					} else { // In replace block
						if(term == NULL)
							printf("%s%s - Couldn't create rule: Failed to collect term in response, line #%d%s\n", c_red, filepath, line_number, c_def);
						regmatch_t value[6];
						if(term[0] == '*')
							rule.replace[i][j-5].type = -1;
						else if (regexec(&color, term, 2, value, 0) == 0) {
							rule.replace[i][j-5].type = 0;
							char num_string[7];
							strncpy(num_string, term + value[1].rm_so, 6);
							rule.replace[i][j-5].value = (uint32_t)strtol(num_string, NULL, 16) + (uint32_t)(255 << 24);
						} else if (regexec(&reference, term, 3, value, 0) == 0) {
							rule.replace[i][j-5].type = 1;
							char* num_string = reggrab(term, value[1]);
							rule.replace[i][j-5].refX = strtol(num_string, NULL, 10);
							free(num_string);

						
							num_string = reggrab(term, value[2]);
							rule.replace[i][j-5].refY = strtol(num_string, NULL, 10);
							free(num_string);
						} else if (regexec(&edit, term, 6, value, 0) == 0) {
							rule.replace[i][j-5].type = 2;
							char* num_string = reggrab(term, value[1]);
							rule.replace[i][j-5].refX = strtol(num_string, NULL, 10);
							free(num_string);

							num_string = reggrab(term, value[2]);
							rule.replace[i][j-5].refY = strtol(num_string, NULL, 10);
							free(num_string);

							int8_t r, g, b;

							num_string = reggrab(term, value[3]);
							r = strtol(num_string, NULL, 10);
							free(num_string);

							num_string = reggrab(term, value[4]);
							g = strtol(num_string, NULL, 10);
							free(num_string);

							num_string = reggrab(term, value[5]);
							b = strtol(num_string, NULL, 10);
							free(num_string);

							rule.replace[i][j-5].value = ((uint32_t)((uint8_t)r)<<16) + ((uint32_t)((uint8_t)g)<<8) + ((uint32_t)(uint8_t)b);
						} else if (regexec(&identity, term, 2, value, 0) == 0) {
							rule.replace[i][j-5].type = 3;
							char* identity_name = reggrab(term, value[1]);
							rule.replace[i][j-5].value = getIdentity(identity_name);
							if(rule.replace[i][j-5].value==-1){
								rule.replace[i][j-5].value = n_identities;
								identities = realloc(identities, sizeof(struct identity)*(n_identities+1));
								identities[n_identities].member_count = 0;
								identities[n_identities].members = NULL;
								identities[n_identities].name = identity_name; // No need to strcpy 'i'
								n_identities++;
								// printf("%s%s - Couldn't create rule: Unknown identity \"%s\" referenced in replace! Line #%d%s\n", c_red, filepath, identity_name, line_number, c_def);
							}
						} else {
							printf("%s%s - Couldn't create rule: Unknown replacement term syntax, match term #%d, rule row #%d, Line #%d%s\n", c_red, filepath, j, i, line_number, c_def);
							
							while(i < 5) { // Speed through rest of rule, ignoring contents
								i++;
								if(fgets(rule_str, 255, f)); // prevent unused result warning
								line_number++;
							}
							j = 10;
							break;
						}
					}


					if(needToFree)
						free(term);
					offset += regmatch[1].rm_eo;
				}
			}
			
			rule.search_for = (struct match_t*)malloc(sizeof(struct match_t)*rule.num_search_for);
			for(int i = 0; i<rule.num_search_for; i++)
				rule.search_for[i] = unique_members[i];
			free(unique_members);

			regmatch_t chance_grab[2];
			if(regexec(&chance, line, 2, chance_grab, 0) == 0) {
				char* num_string = reggrab(line, chance_grab[1]);
				rule.chance = strtof(num_string, NULL) / 100.f;
				free(num_string);
			}
			rules[n_rules] = rule;
			n_rules++;
			if (regexec(&x_mirror, line, 0, NULL, 0)==0) {
				// Create x-mirrored version of rule
				rules[n_rules] = rule;
				for(int i = 0; i < 5; i++) {
					for(int j = 0; j < 5; j++) {
						rules[n_rules].match[i][j] = rule.match[i][4-j];
						rules[n_rules].replace[i][j] = rule.replace[i][4-j];
						if(rules[n_rules].replace[i][j].type>=1)// is reference or edit
							rules[n_rules].replace[i][j].refX *= -1;
					}
				}
				n_rules++;
			}
			if (regexec(&y_mirror, line, 0, NULL, 0)==0) {
				// Create y-mirrored version of rule
				rules[n_rules] = rule;
				for(int i = 0; i < 5; i++) {
					for(int j = 0; j < 5; j++) {
						rules[n_rules].match[i][j] = rule.match[4-i][j];
						rules[n_rules].replace[i][j] = rule.replace[4-i][j];
						if(rules[n_rules].replace[i][j].type>=1)// is reference or edit
							rules[n_rules].replace[i][j].refY *= -1;
					}
				}
				if(regexec(&x_mirror, line, 0, NULL, 0) == 0) {
					// y-mirrored AND x-mirrored
					// Perform y-mirror on (n_rules-1)/x-mirrored rule
					n_rules++;
					rules[n_rules] = rule;
					for(int i = 0; i < 5; i++) {
						for(int j = 0; j < 5; j++) {
							rules[n_rules].match[i][j] = rule.match[4-i][4-j];
							rules[n_rules].replace[i][j] = rule.replace[4-i][4-j];
							if(rules[n_rules].replace[i][j].type>=1) {// is reference or edit
								rules[n_rules].replace[i][j].refX *= -1;
								rules[n_rules].replace[i][j].refY *= -1;
							}
						}
					}
				}
				n_rules++;
			}

		} else {
			regmatch_t grab[3];
			
			if((ret = regexec(&is_symbol_def, line, 2, grab, 0))==0) {
				symbols[n_symbols] = line[0];
				resolved[n_symbols] = reggrab(line, grab[1]);
				n_symbols++;
			} else if((ret = regexec(&element, line, 3, grab, 0))==0) {
				char* element_color_str = reggrab(line, grab[1]);
				char* element_bind = reggrab(line, grab[2]);
				uint32_t element_color = strtol(element_color_str, NULL, 16) + (255 << 24);
				free(element_color_str);
				if(element_bind!=NULL)
					binds[toupper(element_bind[0])] = element_color;
				
				free(element_bind);
				fpos_t saved;
				fgetpos(f, &saved);
				char element_identities[255];
				while(fgets(element_identities, 255, f)) {
					if(regexec(&element_identity, element_identities, 2, grab, 0)==0) {
						line_number++;
						char* i = reggrab(element_identities, grab[1]);
						int identity_index = getIdentity(i);
						if(identity_index != -1) {
							if(isIdentity(identity_index, element_color))
								continue;
							struct identity *id = identities + identity_index;
							if(id->members)
								id->members = realloc(id->members, sizeof(uint32_t)*id->member_count+1);
							else
								id->members = (uint32_t*)malloc(sizeof(uint32_t)*id->member_count+1);
							id->members[id->member_count] = element_color;
							id->member_count++;
						} else {
							identities = realloc(identities, sizeof(struct identity)*(n_identities+1));
							identities[n_identities].member_count = 1;
							identities[n_identities].members = (uint32_t*)malloc(sizeof(uint32_t));
							identities[n_identities].members[0] = element_color;
							identities[n_identities].name = reggrab(element_identities, grab[1]); // No need to strcpy 'i'
							n_identities++;
						}
						fgetpos(f, &saved);
					} else {
						fsetpos(f, &saved);
						break;
					}
				}
			} else {
				char err[2048];
				ret = regerror(ret, &is_symbol_def, err, 2048);
				printf("%s%s - Couldn't create rule: Unable to parse line #%d, did you intend to write a symbol => definition? Symbols can only be 1 character%s\n", c_red, filepath, line_number, c_def);
			}
		}
	}
	
	// TODO FREE ALL MEMORY WHENEVER WE RETURN PREMATURELY
	fclose(f);
}

void updateRegions() {
	for(int i = 0; i < 4; i++) {
		quadrants[i].num_unique_members = 0;
		for(int j = 0; j < 4; j++)
			quad_quadrants[i*4+j].num_unique_members = 0;
	}

	for(int i = 0; i < WIDTH; i++) {
		for(int j = 0; j < HEIGHT; j++) {
			int col = get(i, j);
			struct region* quadrant = quadrants + (2*i/WIDTH) + 2*(2*j/HEIGHT);
			if(!isUIntMemberOf(col, quadrant->unique_members, quadrant->num_unique_members))
				quadrant->unique_members[quadrant->num_unique_members++] = col;
			struct region* quad_quadrant = quad_quadrants + (4*i/WIDTH) + 4*(4*j/HEIGHT);
			if(!isUIntMemberOf(col, quad_quadrant->unique_members, quad_quadrant->num_unique_members))
				quad_quadrant->unique_members[quad_quadrant->num_unique_members++] = col;
		}
	}

}

int main(int argc, char* argv[]) {
	srand(time(NULL));
	// Just double checking that (uint32_t)-1 == 0xffffffff, which it is
	//printf("%u", (uint32_t)-1);
	
	SDL_Init(SDL_INIT_VIDEO);
	SDL_Window* w;
	if((w = SDL_CreateWindow("Sand", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, WIDTH*WINDOW_SCALE, HEIGHT*WINDOW_SCALE, SDL_WINDOW_OPENGL))==NULL)
		return 1;
	window_surface = SDL_GetWindowSurface(w);
	SDL_Renderer *renderer = SDL_CreateRenderer(w, -1, SDL_RENDERER_ACCELERATED);
	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
	surf = SDL_CreateRGBSurface(0, WIDTH, HEIGHT, 32, 0, 0, 0, 0);
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, 0);
	SDL_Texture *texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_BGRA32, SDL_TEXTUREACCESS_STREAMING, WIDTH, HEIGHT);
	SDL_Rect screenRect;
	screenRect.x = 0; screenRect.y = 0; screenRect.w = WIDTH*WINDOW_SCALE; screenRect.h = HEIGHT*WINDOW_SCALE;

	// Set up quadrants and quadquadrants
	quadrants = (struct region*)malloc(sizeof(struct region)*4);
	quad_quadrants = (struct region*)malloc(sizeof(struct region)*16);
	for(int i = 0; i < 4; i++) {
		quadrants[i].num_unique_members = 1;
		quadrants[i].unique_members = (uint32_t*)malloc(sizeof(uint32_t)*(WIDTH/2)*(HEIGHT/2));
		quadrants[i].unique_members[0] = AIR; // We always start with AIR everywhere
		for(int j = 0; j < 4; j++) {
			quad_quadrants[i*4+j].num_unique_members = 1;
			quad_quadrants[i*4+j].unique_members = (uint32_t*)malloc(sizeof(uint32_t)*(WIDTH/4)*(HEIGHT/4));
			quad_quadrants[i*4+j].unique_members[0] = AIR; // We always start with AIR everywhere
		}
	}

	
	// Setup identities
	identities = (struct identity*)malloc(sizeof(struct identity)*1);
	
	// Set up rules
	for(int i = 0; i < 255; i++)
		binds[i] = 0;
	rules = (struct rule*)malloc(sizeof(struct rule)*MAX_RULES);
	DIR *dir;
	struct dirent *ent;
	if((dir = opendir("./rules"))!=NULL) {
		while((ent=readdir(dir))!=NULL) {
			printf("%s\n",ent->d_name);
			char* rule_file = (char*)malloc(8+strlen(ent->d_name));
			strcpy(rule_file, "./rules/");
			strcat(rule_file, ent->d_name);
			loadRule(rule_file);
		}
		closedir(dir);
	}

	printf("Rule match and replace types\n");
	for(int i = 0; i < 5; i++) {
		for(int j = 0; j < 5; j++)
			printf(" %d ", rules[2].match[i][j].type);
		printf("\t\t");
		for(int j = 0; j < 5; j++)
			printf(" %d ", rules[2].replace[i][j].type);
		printf("\n");
	}
			
	for(int i = 0; i<WIDTH; i++)
		for(int j = 0; j < HEIGHT; j++)
			put(AIR, i, j);
	// A list of rule indices that will be shuffled over time, so the rules won't be enacted in the exact same order every time
	int *rule_list = (int*) malloc(sizeof(int)*n_rules);
	for(int i = 0; i < n_rules; i++)
		rule_list[i] = i;
	
	float paint_size = 1;
	bool paint_once = false;
	uint32_t SELECTED_ELEMENT = AIR;
	uint32_t step = 0;
	
	SDL_Event ev;
	int running = 1;
	while(running) {
		while(SDL_PollEvent(&ev)) {
			// Events
			if(ev.type==SDL_QUIT) {
				running = 0;
				break;
			}
			
			if(ev.type==SDL_MOUSEBUTTONDOWN && ev.button.button == SDL_BUTTON_LEFT) {
				mouseLeft = true;
			}
			if(ev.type==SDL_MOUSEBUTTONUP && ev.button.button == SDL_BUTTON_LEFT) {
				mouseLeft = false;
			}
			
			if(ev.type==SDL_MOUSEMOTION) {
				mouseX = ev.motion.x/WINDOW_SCALE;
				mouseY = ev.motion.y/WINDOW_SCALE;
				relX = ev.motion.xrel/WINDOW_SCALE;
				relY = ev.motion.yrel/WINDOW_SCALE;
			}
			
			if(ev.type==SDL_MOUSEWHEEL) {
				paint_size += paint_size * ev.wheel.y * 0.1;
				if(paint_size < 1) paint_size = 1;
			}
			
			if(ev.type==SDL_KEYDOWN) {
				const char* keycode = SDL_GetScancodeName(ev.key.keysym.scancode);
				if(strlen(SDL_GetScancodeName(ev.key.keysym.scancode))==1)
					if(binds[SDL_GetScancodeName(ev.key.keysym.scancode)[0]] != 0)
						SELECTED_ELEMENT = binds[SDL_GetScancodeName(ev.key.keysym.scancode)[0]];
				switch(ev.key.keysym.scancode) {
				case SDL_SCANCODE_LCTRL:
					paint_once = true;
					break;
				}
			}
			
			if(ev.type==SDL_KEYUP) {
				switch(ev.key.keysym.scancode) {
				case SDL_SCANCODE_LCTRL:
					paint_once = false;
					break;
				}
			}
		}
		
		if(mouseLeft && mouseX >= 0 && mouseX < WIDTH && mouseY >= 0 && mouseY < HEIGHT) {
			for(int x = mouseX+1 - paint_size; x < mouseX + paint_size; x++)
				for(int y = mouseY+1 - paint_size; x>=0&&x<WIDTH&&y < mouseY + paint_size; y++)
					if(y>=0&&y<HEIGHT)
						put(SELECTED_ELEMENT, x, y);
			if(paint_once)
				mouseLeft = false;
		}
		
		// Simulate bottom to top for style
		for(int iter = 0; iter < ITERATIONS; iter++) {
			int ystart = HEIGHT+3 - (step/STEPPING);
			int xstart = -4 + (step%STEPPING);
			for(int j = ystart; j > -5; j-=STEPPING)
				for(int i = ((step%2)==0?xstart:(WIDTH-xstart)); i < WIDTH+5 && i > -5; i+=((step%2)==0?1:-1)*STEPPING)
					for(int r = 0; r < n_rules; r++) {
						if(rules[rule_list[r]].replace[2][3].refX == -2 && i == 1 && j == 1)
							get(i, j);
						if(matches(rules[rule_list[r]], i, j)) {
							enforce(rules[rule_list[r]], i, j);
						}
					}
			int shuffle_a = rand() % n_rules;
			int shuffle_b = rand() % n_rules;
			int shuffle_z = rule_list[shuffle_a];
			rule_list[shuffle_a] = rule_list[shuffle_b];
			rule_list[shuffle_b] = shuffle_z;
			step++;
			if(step>=STEPPING*STEPPING)
				step = 0;
		}

		updateRegions();
		

		SDL_UpdateTexture(texture, &screenRect, surf->pixels, surf->pitch);
		SDL_RenderClear(renderer);
		SDL_RenderCopy(renderer, texture, NULL, NULL);
		SDL_Rect preview;
		preview.x = floor(mouseX+1-paint_size)*WINDOW_SCALE; preview.w = floor(mouseX + paint_size)*WINDOW_SCALE-preview.x;
		preview.y = floor(mouseY+1-paint_size)*WINDOW_SCALE; preview.h = floor(mouseY + paint_size)*WINDOW_SCALE-preview.y;
		SDL_SetRenderDrawColor(renderer, fmin(((SELECTED_ELEMENT & 0x00ff0000)>>16)+16, 255), fmin(((SELECTED_ELEMENT & 0x0000ff00)>>8)+16,255), fmin((SELECTED_ELEMENT & 0x000000ff)+16,255), 128);
		SDL_RenderFillRect(renderer, &preview);
		SDL_RenderPresent(renderer);
	}
	
	
	SDL_DestroyWindow(w);
	return 0;
}