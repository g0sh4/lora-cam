import serial
import time
import os

# --- Konfiguracja ---
# Proszę zmieniać te wartości przed każdym nowym testem
SF = "SF7"
d = "1km"

# Ustawienia portu szeregowego
SERIAL_PORT = 'COM11' 
BAUD_RATE = 115200
# --- Koniec ---

START_MARKER = 'START_IMAGE_TRANSFER'

def log(message, log_file_handle):
    """Wypisuje wiadomość na konsoli i zapisuje ją do pliku logu."""
    # Dodajemy timestamp do każdej linii logu dla precyzyjnej analizy
    timestamp = time.strftime("%H:%M:%S")
    log_message = f"[{timestamp}] {message}"
    
    print(log_message)
    if log_file_handle:
        log_file_handle.write(log_message + '\n')
        log_file_handle.flush() # Zapisuje dane do pliku na bieżąco

def main():
    # --- NOWA SEKCJA: Logowanie do pliku ---
    # Tworzenie nazwy pliku logu na podstawie parametrów testu i aktualnego czasu
    run_timestamp = time.strftime("%Y%m%d-%H%M%S")
    log_filename = f"{SF}_{d}_{run_timestamp}.log"
    
    # Tworzenie dedykowanego folderu na wyniki, jeśli nie istnieje
    results_dir = "wyniki_testow"
    os.makedirs(results_dir, exist_ok=True)
    
    log_filepath = os.path.join(results_dir, log_filename)

    # Otwieramy plik logu, który będzie aktywny przez cały czas działania skryptu
    with open(log_filepath, 'w', encoding='utf-8') as log_file:
        log(f"Skrypt odbiornika obrazów uruchomiony. Logowanie do pliku: {log_filepath}", log_file)
        log(f"Parametry testu: SF={SF}, Dystans={d}", log_file)
        log(f"Oczekiwanie na połączenie na porcie {SERIAL_PORT}...", log_file)

        try:
            with serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=2) as ser:
                log("Połączono! Oczekiwanie na dane z Węzła C...", log_file)
                
                while True:
                    try:
                        line = ser.readline().decode('utf-8', errors='ignore').strip()

                        if not line:
                            continue # Pomiń puste linie (timeout)

                        image_status = "UNKNOWN"
                        if line.startswith("STATUS:"):
                            image_status = line.split(":")[1].strip()
                            log(f"[Arduino C]: {line}", log_file)
                            line = ser.readline().decode('utf-8', errors='ignore').strip()

                        if line == START_MARKER:
                            log(f"[Arduino C]: {line}", log_file) # Logowanie markera
                            log(f"\n[+] Wykryto marker startu (Status z Arduino: {image_status})!", log_file)

                            size_line = ser.readline().decode('utf-8').strip()
                            log(f"[Arduino C]: {size_line}", log_file) # Logowanie linii z rozmiarem
                            
                            if not size_line.isdigit():
                                log(f"[!] Błąd: Oczekiwano rozmiaru obrazu, a otrzymano: '{size_line}'", log_file)
                                continue
                            
                            expected_image_size = int(size_line)
                            log(f"[*] Oczekiwany (pełny) rozmiar obrazu: {expected_image_size} bajtów.", log_file)

                            log(f"[*] Odbieranie danych obrazu...", log_file)
                            image_data = ser.read(expected_image_size)
                            
                            is_complete = len(image_data) == expected_image_size
                            
                            image_timestamp = time.strftime("%Y%m%d-%H%M%S")
                            status_suffix = "_INCOMPLETE" if not is_complete or image_status == "INCOMPLETE" else ""
                            
                            # Ulepszona nazwa pliku obrazu
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

                        elif line:
                            log(f"[Arduino C]: {line}", log_file)

                    except Exception as e:
                        log(f"[BŁĄD KRYTYCZNY W PĘTLI] Wystąpił wyjątek: {e}", log_file)
                        pass

        except serial.SerialException as e:
            log(f"BŁĄD KRYTYCZNY: Nie można otworzyć portu {SERIAL_PORT}.", log_file)
            log(f"Sprawdź połączenie i nazwę portu.", log_file)
        except KeyboardInterrupt:
            log("\nProgram zatrzymany przez użytkowniczkę.", log_file)

if __name__ == '__main__':
    main()