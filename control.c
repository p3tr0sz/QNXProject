#include <stdlib.h>
#include <sys/neutrino.h>
#include <sys/iofunc.h>
#include <sys/dispatch.h>
#include <errno.h>
#include <sys/siginfo.h>
#include <signal.h>
#include <termios.h>
#include <unistd.h>
#include <sys/wait.h>
#include <ncurses.h>

#define SIZE 256
#define NAME "serwer"
#define FLAG NAME_FLAG_ATTACH_GLOBAL
#define MY_TIMER_EVENT (_PULSE_CODE_MINAVAIL + 2)

/*struktura na przechowywanie wiadomosci i odpowiedzi od klienta i timera*/

struct{
	int type;
	char text[SIZE];
}msg_c,rmsg_c,msg_ti,rmsg_ti;

/*******funkcja zwracajaca 1 jesli nastapilo zdarzenie wcisniecia jakiegokolwiek klawisza*******/
int kbhit(void)
{
    int ch = getch();

    if (ch != ERR) {
        ungetch(ch);
        return 1;
    } else {
        return 0;
    }
}
/***********************************Funkcja sterujaca*********************************/
double control_test(double H, WINDOW *window){
	double dQ1 = 5;
	double Q0 = 34.789;
	double Q1;

	mvwprintw(window,10,3,"Poziom cieczy H: %lf", H);
	wrefresh(window);
	wmove(window,11,3);
	wclrtoeol(window);
	wrefresh(window);
	if(H <= 1.1){
		Q1 = Q0 + dQ1;
		if(H <= 1.05){
			mvwprintw(window,11,3,"Poziom cieczy niski");
			wrefresh(window);
		}
	}else if((H>1.1) && (H<=1.3)){
		Q1 = Q0 + dQ1;
		if(H > 1.25){
			mvwprintw(window,11,3,"Poziom cieczy wysoki");
			wrefresh(window);
		}
	}else if(H >= 1.3){
		Q1 = Q0 - dQ1;
		mvwprintw(window,11,3,"Poziom cieczy wysoki");
		wrefresh(window);
	}
	return Q1;
}

int main(int argc, char *argv[]){

	WINDOW *window;

	//deklaracje zmiennych
	int server_pid;
	int client_chid;
	int timer_chid;
	int client_rcvid;
	int timer_rcvid;
	int timer_coid;
	int system_command;
	int id;
	int boo;

	//zmienne okreslajace wyglad okna
	int startx, starty, width, height;

	struct _msg_info info_c;
	struct _msg_info info_t;
	name_attach_t *server_name;
	struct sigevent event;

	timer_t timid;
	struct itimerspec t;

	double H = 1.0;
	double Q1;
	boo = 1;

	height = 18;
	width = 78;
	starty = 5;
	startx = 0;

	//inicjalizacja biblioteki ncurses w celu lepszej obslugi terminala tekstowego
	initscr();	//rozpoczecie trypu pracy
	noecho();	//nie wyswietla wczytywanego znaku na terminalu
	cbreak();

	nodelay(stdscr, TRUE);


	/***********Zamkniecie dotychczasowych GNS i utworzenie nowych********************/

	system_command = system("slay gns");
	if(system_command == -1){
		printw("Nie bylo uruchomionego GNS, program GNS zostanie uruchomiony\n");
		refresh();
	}
	else{
		printw("Uruchomiony program GNS skasowany, zostanie uruchomiony nowy\n");
		refresh();
	}
	system_command = system("/usr/sbin/gns -s serwer");
	if(system_command == -1){
		printw("GNS nie mogl zostac uruchomiony, rezultat: %d\n", WEXITSTATUS( system_command ));
		refresh();
	}

	/********************Utworzenie i rejestracja nazwy oraz kanalu dla klienta***********/
	server_name = name_attach(NULL,NAME,FLAG);
	if(server_name == NULL){
		printw("Rejestracja");
		return EXIT_FAILURE;
	}

	client_chid = server_name->chid;
	if (client_chid == -1){

		printw("Nie moge utworzyc kanalu dla klienta: %s\n", strerror(errno));
		refresh();
		return EXIT_FAILURE;
	}
	/**************Utworzenie kanalu dla timera*********/

	timer_chid = ChannelCreate(0);
	if (timer_chid == -1){
		printw("Nie moge utworzyc kanalu dla timera: %s\n", strerror(errno));
		refresh();
		return EXIT_FAILURE;
	}

	server_pid = getpid();

	/*********** utworzenie polaczenia do samego siebie**********/
	timer_coid = ConnectAttach(0,server_pid,timer_chid,_NTO_SIDE_CHANNEL,0);
	if (timer_coid == -1) {
		printw( "Nie moge utworzyc polaczenia dla timera: %s\n", strerror(errno) );
		refresh();
		return EXIT_FAILURE;
	}

	/*************Utworzenie okna z wykorzystaniem ncurses*********/
	window = newwin(height, width, starty, startx);
	box(window, 0 , 0);

	mvwprintw(window,2,3,"Oprogramowanie do sterowania i symulacji poziomu cieczy w zbiorniku");
	mvwprintw(window,3,3,"Wcisniecie klawisza 'q' zamknie program");
	mvwprintw(window,5,3,"Nazwa: %s, kanal: %d",NAME, client_chid);

	wrefresh(window);

	/********** wypelnienie struktury event*****************/
	SIGEV_PULSE_INIT( &event, timer_coid, getprio(0), MY_TIMER_EVENT, 0);

	/*****************************Utworzenie timera********************/
	id = timer_create(CLOCK_REALTIME,&event,&timid);
	if(id < 0){
		mvwprintw(window,6,4,"Blad utworzenia timera");
		wrefresh(window);
		exit(-1);
	}

	/*************Nastawienie timera na 25ms ************/
	t.it_value.tv_sec = 0;
	t.it_value.tv_nsec = 25000000;
	t.it_interval.tv_sec = 0;
	t.it_interval.tv_nsec = 25000000;
	timer_settime(timid,0,&t,NULL);

	while(boo){

		//sprawdzenie czy wcisniety zostal klawisz q
		 if (kbhit() && getch() == 113) {
			 boo = 0;
			// wrefresh(window);
		 }
		//odbieranie wiadomosci od timera
		timer_rcvid = MsgReceive(timer_chid,&msg_ti,sizeof(msg_ti),&info_t);
		if(timer_rcvid == -1){
				mvwprintw(window,7,4,"Blad timera: %d", errno);
				wrefresh(window);
				continue;
			}

			//jesli timer dostarczy impuls to odbierana jest wiadomosc od klienta
			if(timer_rcvid == 0) {
				client_rcvid = MsgReceive(client_chid,&msg_c,sizeof(msg_c),&info_c);
				if(client_rcvid == -1){
					mvwprintw(window,8,4,"Blad odbioru id klienta: %d", errno);
					wrefresh(window);
					continue;
				}
				//jesli klient nie wysyla wiadomosci to znaczy ze zostal wylaczony
				if(client_rcvid == 0){
					mvwprintw(window,13,3,"Klient zakonczyl prace");
					wrefresh(window);
					break;
				}else{	//jesli wszystko pojdzie z planem to odbior poziomu od klienta
					H = atof(msg_c.text);
					Q1 = control_test(H,window);

					//kopiowanie wartosci Q1 do wysylanej wiadomosci
					sprintf(msg_c.text,"%lf",Q1);
					//wyslanie odpowiedzi do klienta
					MsgReply(client_rcvid,0,&msg_c,sizeof(msg_c));
				}
			}
		}
	mvwprintw(window,14,3,"Konczenie pracy serwera...");
	wrefresh(window);
	ConnectDetach(timer_coid);
	ChannelDestroy(timer_chid);
	name_detach(server_name,0);

	/*******Zamkniecie wykorzystywanego GNS*******/
	system_command = system("slay gns");
	if(system_command == -1){
		mvwprintw(window,15,3,"Nie mozna zamknac uzywanego programu GNS");
		wrefresh(window);
	}else{
		mvwprintw(window,15,3,"Zakonczenie pracy serwera.");
		wrefresh(window);
	}
	endwin();
	return EXIT_SUCCESS;
}
