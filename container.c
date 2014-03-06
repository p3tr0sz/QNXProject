#include <stdio.h>
#include <stdlib.h>
#include <sys/neutrino.h>
#include <sys/iofunc.h>
#include <sys/dispatch.h>
#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <ncurses.h>

#define SIZE 256
#define NAME "serwer"
#define FLAG NAME_FLAG_ATTACH_GLOBAL


/******stale potrzebne przy obliczeniach***********/
const double g = 9.81;
const double gestosc = 1000;
const double A1 = 4*atan(1.0)/4;
const double A2 = 4*atan(1.0)/400;

/*************Struktura parametrow dla watkow****************/

typedef struct{

	double Q1;
	double Q2;
	double dH;
	double H;
	double t;
	//funkcja prawych stron (zmiana poziomu cieczy)
	double(*fun)(double,double,double); //(t,H,Q1)
}pthread_args;

/***********Struktura na wiadomosci oraz odpowiedzi**********/
struct{
	int type;
	char text[SIZE];
}msg,rmsg;

/***********Funkcja obliczajaca zmiane poziomu cieczy*********/
double zmiana(double t, double H, double Q1){
	double dH = Q1/(gestosc*A1) - (A2/A1)*sqrt(2*g*H);
	return dH;
}

/******Watek obliczajacy zmiane poziomu cieczy********/
void *zmianapoziomu(void *argms){

	pthread_args *args = (pthread_args*) argms;
	double Q1 = args->Q1;
	double H = args->H;
	double t = args->t;
	args->fun = zmiana;
	args->dH = args->fun(t,H,Q1);

	pthread_exit(NULL);
}

/*******Watek calkujacy metoda rungego-kutty******/
void *runge(void *argms){

	double dt,K1,K2,K3,K4;
	dt = 0.05;

	pthread_args *args = (pthread_args*) argms;
	double Q1 = args->Q1;
	double H = args->H;
	double t = args->t;
	args->fun = zmiana;

	K1 = (dt * args->fun(t,H,Q1));
	K2 = (dt * args->fun((t + dt/2), (H + K1/2),Q1));
	K3 = (dt * args->fun((t + dt/2), (H + K2/2),Q1));
	K4 = (dt * args->fun((t + dt), (H + K3),Q1));
	H = H + (K1 + 2*K2 + 2*K3 + K4)/6;
	args->H = H;

	pthread_exit(NULL);
}

/*******funkcja zwracajaca 1 jesli nastapilo zdarzenie wcisniecia jakiegokolwiek klawisza*******/
int kbhit(void){
    int ch = getch();
    if (ch != ERR) {
        ungetch(ch);
        return 1;
    } else {
        return 0;
    }
}

int main(int argc, char *argv[]) {

	WINDOW *window;

	pthread_args *args = malloc(sizeof (*args));

	int coid,res,boo;
	FILE *fd;
	pthread_t watek1,watek2;
	time_t time_of_day;
	int startx, starty, width, height;
	char filename[101];
	char choice;

	args->Q1 = 34.789;
	args->H=1.0;
	boo = 1;
	choice = 'c';
	height = 22;
	width = 78;
	starty = 1;
	startx = 0;

	initscr();
	noecho();
	cbreak();

	nodelay(stdscr, TRUE);
	scrollok(stdscr, TRUE);

	refresh();

	window = newwin(height, width, starty, startx);
	box(window, 0 , 0);
	wrefresh(window);

	mvwprintw(window,2,3,"Oprogramowanie do sterowania i symulacji poziomu cieczy w zbiorniku");
	mvwprintw(window,3,3,"Wcisniecie klawisza 'q' zamknie program");
	mvwprintw(window,5,3,"Pojemnik: Lokalizuje serwer...");

	time_of_day = time( NULL );

	/* Tworzenie polaczenia */
	coid = name_open(NAME, FLAG);

	if (coid == -1)
	{
		mvwprintw(window,7,4,"Pojemnik: Nie moge utworzyc polaczenia: %s",strerror(errno));
		wrefresh(window);
		exit(EXIT_FAILURE);
	}

	//otwarcie pliku do zapisu i wpisanie aktualnej daty i godziny
	fd = fopen("data.txt", "w");
	fprintf(fd,"Data: %s\n", ctime(&time_of_day));
	fprintf(fd,"Q1\t\tH\t\tQ2\t\tdH\n");

	while(boo){

		//oczekiwanie na wcisniecie klawisza q
		if (kbhit() && getch() == 113) {
			boo = 0;
		}
		//skopiowanie biezacej wartosci poziomu cieczy do bufora wiadomosci
		sprintf(msg.text,"%lf\n",args->H);
		mvwprintw(window,10,3,"Poziom cieczy H: %lf", args->H);
		wrefresh(window);
		//przeslanie poziomu cieczy do serwera
		res = MsgSend(coid,&msg,sizeof(msg),&rmsg,sizeof(rmsg));

		if(res == -1){
			mvwprintw(window,8,3,"Nie moge przeslac wiadomosci lub serwer zakonczony");
			wrefresh(window);
			break;
		}

		args->Q1 = atof(rmsg.text);
		mvwprintw(window,11,3,"Wydatek Q1: %lf", args->Q1);
		wrefresh(window);
		args->Q2 = (A2/A1)*sqrt(2*g*(args->H));
		mvwprintw(window,12,3,"Wydatek Q2: %lf",args->Q2);
		wrefresh(window);
		pthread_create(&watek1,NULL,&zmianapoziomu,(void*)(args));
		mvwprintw(window,13,3,"Zmiana poziomu cieczy dH: %lf", args->dH);
		wrefresh(window);
		fprintf(fd,"%lf\t%lf\t%lf\t%lf\n", args->Q1, args->H, args->Q2, args->dH);
		if(args->H <= 1.05){
			mvwprintw(window,14,3,"Poziom cieczy niski");
			wrefresh(window);
		}
		else if(args->H > 1.25){
			mvwprintw(window,14,3,"Poziom cieczy wysoki");
			wrefresh(window);
		}else{
			wmove(window,14,3);
			wclrtoeol(window);
			wrefresh(window);
		}
		pthread_create(&watek2,NULL,&runge,(void*)(args));

	}

	name_close(coid);
	wrefresh(window);
	mvwprintw(window,16,3,"Konczenie pracy klienta...");
	fclose(fd);
	free(args);
	wrefresh(window);


	echo(); //wyswietlanie znakow z klawiatury w terminalu
	nodelay(stdscr, FALSE);//opoznienia spowodowane wczytywaniem znaku

	//oczekiwanie na wybor zapisu do pliku lub jego braku
	while((choice != 'y') && (choice != 'n')){
		wmove(window,17,3);
		wclrtoeol(window);
		mvwprintw(window,17,3,"Zapisac do pliku? (y/n): ");
		wrefresh(window);
		wmove(window,17,28);
		wclrtoeol(window);
		mvwscanw(window,17,28,"%c", &choice);
		if(choice == 'y'){
			wmove(window,18,3);
			wclrtoeol(window);
			mvwprintw(window,18,3,"Podaj nazwe pliku do zapisu *.txt: ");
			wrefresh(window);
			wmove(window,18,38);
			wclrtoeol(window);
			mvwscanw(window,18,38,"%s", &filename);
			//zmiana nazwy pliku
			rename("data.txt",filename);
			break;
		}else if(choice == 'n'){
			wmove(window,18,3);
			wclrtoeol(window);
			mvwprintw(window,18,3,"Wybrales brak zapisu\n");
			wrefresh(window);
			//usuniecie pliku
			remove("data.txt");
			break;
		}else{
			wmove(window,18,3);
			wclrtoeol(window);
			mvwprintw(window,17,3,"Bledny wybor, wpisz jeszcze raz\n");
			wrefresh(window);
		}
		}
	endwin();
	return EXIT_SUCCESS;
}

