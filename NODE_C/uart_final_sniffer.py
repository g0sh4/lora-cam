import serial
import time
import os

# --- Konfiguracja ---
# Proszę zmieniać te wartości przed każdym nowym testem
SF = "SF7"
d = "1"

# Windows: 'COM3', 'COM4' itp. | Linux: '/dev/ttyACM0' | macOS: '/dev/tty.usbmodemXXXX' 
SERIAL_PORT = 'COM11' 
BAUD_RATE = 115200

# Ten marker nie uległ zmianie w kodzie C++. Skrypt wciąż będzie go nasłuchiwał.
START_MARKER = 'START_IMAGE_TRANSFER'

def log(message, log_file_handle):
    """Wypisuje wiadomość na konsoli i zapisuje ją do pliku logu."""
    timestamp = time.strftime("%H:%M:%S")
    log_message = f"[{timestamp}] {message}"
    
    print(log_message)
    if log_file_handle:
        log_file_handle.write(log_message + '\n')
        log_file_handle.flush()

def main():
    # --- Logowanie do pliku (bez zmian) ---
    run_timestamp = time.strftime("%Y%m%d-%H%M%S")
    log_filename = f"{SF}_{d}_{run_timestamp}.log"
    
    results_dir = "wyniki_testow"
    os.makedirs(results_dir, exist_ok=True)
    
    log_filepath = os.path.join(results_dir, log_filename)

    with open(log_filepath, 'w', encoding='utf-8') as log_file:
        log(f"Skrypt odbiornika obrazów uruchomiony. Logowanie do pliku: {log_filepath}", log_file)
        log(f"Parametry testu: SF={SF}, Dystans={d}", log_file)
        log(f"Oczekiwanie na połączenie na porcie {SERIAL_PORT}...", log_file)

        try:
            with serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=2) as ser:
                log("Połączono! Oczekiwanie na dane z Węzła C...", log_file)
                
                while True:
                    try:
                        # Odczytujemy linie z logami z ESP32, aż znajdziemy marker startu
                        line = ser.readline().decode('utf-8', errors='ignore').strip()

                        if not line:
                            continue

                        # Logowanie statusu (COMPLETE/INCOMPLETE) wysyłanego przez Węzeł C
                        image_status = "UNKNOWN"
                        if line.startswith("STATUS:"):
                            image_status = line.split(":")[1].strip()
                            log(f"[Arduino C]: {line}", log_file)
                            # Po statusie spodziewamy się markera, więc czytamy kolejną linię
                            line = ser.readline().decode('utf-8', errors='ignore').strip()

                        # --- LOGIKA ODBIORU OBRAZU (BEZ ZMIAN) ---
                        # 1. Szukaj markera startu
                        if line == START_MARKER:
                            log(f"[Arduino C]: {line}", log_file) 
                            log(f"\n[+] Wykryto marker startu (Status z Arduino: {image_status})!", log_file)

                            # 2. Odczytaj kolejną linię, która zawiera rozmiar obrazu
                            size_line = ser.readline().decode('utf-8').strip()
                            log(f"[Arduino C]: {size_line}", log_file)
                            
                            if not size_line.isdigit():
                                log(f"[!] Błąd: Oczekiwano rozmiaru obrazu, a otrzymano: '{size_line}'", log_file)
                                continue
                            
                            expected_image_size = int(size_line)
                            log(f"[*] Oczekiwany rozmiar obrazu: {expected_image_size} bajtów.", log_file)

                            # 3. Odczytaj dokładnie tyle surowych bajtów, ile podano w rozmiarze
                            log(f"[*] Odbieranie danych obrazu...", log_file)
                            image_data = ser.read(expected_image_size)
                            
                            # Sprawdzenie, czy odebrano kompletną paczkę danych
                            is_complete = len(image_data) == expected_image_size
                            
                            image_timestamp = time.strftime("%Y%m%d-%H%M%S")
                            # Dodajemy dopisek _INCOMPLETE, jeśli status z Arduino tak wskazuje
                            status_suffix = "_INCOMPLETE" if not is_complete or image_status == "INCOMPLETE" else ""
                            
                            base_filename = f'recived_img_{SF}_{d}_{image_timestamp}{status_suffix}.jpg'
                            image_filepath = os.path.join(results_dir, base_filename)
                            
                            with open(image_filepath, 'wb') as f:
                                f.write(image_data)
                            
                            if is_complete:
                                log(f"[+] SUKCES! Obraz został zapisany do pliku: {image_filepath}", log_file)
                            else:
                                log(f"[!] OSTRZEŻENIE: Zapisano niekompletny obraz do pliku: {image_filepath}", log_file)
                                log(f"    Odebrano {len(image_data)} z oczekiwanych {expected_image_size} bajtów.", log_file)
                            
                            log("\nOczekiwanie na kolejny obraz...", log_file)

                        # Jeśli linia nie jest markerem, po prostu ją loguj
                        elif line:
                            log(f"[Arduino C]: {line}", log_file)

                    except Exception as e:
                        log(f"[BŁĄD KRYTYCZNY W PĘTLI] Wystąpił wyjątek: {e}", log_file)
                        pass

        except serial.SerialException as e:
            log(f"BŁĄD KRYTYCZNY: Nie można otworzyć portu {SERIAL_PORT}.", log_file)
            log(f"Sprawdź połączenie i nazwę portu.", log_file)
        except KeyboardInterrupt:
            log("\nProgram zatrzymany przez użytkownika.", log_file)

if __name__ == '__main__':
    main()