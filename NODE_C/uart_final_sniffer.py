import serial
import time

# --- KONFIGURACJA ---
# ZMIEŃ 'COM3' na właściwy port szeregowy dla Twojego urządzenia.
# Windows: 'COM3', 'COM4' itp. | Linux: '/dev/ttyACM0' | macOS: '/dev/tty.usbmodemXXXX'
SERIAL_PORT = 'COM11' 
BAUD_RATE = 115200
# --- KONIEC KONFIGURACJI ---

START_MARKER = 'START_IMAGE_TRANSFER'

def main():
    print("Skrypt odbiornika obrazów uruchomiony.")
    print(f"Oczekiwanie na połączenie na porcie {SERIAL_PORT}...")

    try:
        with serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=1) as ser:
            print("Połączono! Oczekiwanie na dane z Węzła C...")
            
            while True:
                try:
                    line = ser.readline().decode('utf-8', errors='ignore').strip()

                    if line == START_MARKER:
                        print("\n[+] Wykryto marker startu transmisji obrazu!")

                        size_line = ser.readline().decode('utf-8').strip()
                        if not size_line.isdigit():
                            print(f"[!] Błąd: Oczekiwano rozmiaru obrazu, a otrzymano: '{size_line}'")
                            continue
                        
                        image_size = int(size_line)
                        print(f"[*] Oczekiwany rozmiar obrazu: {image_size} bajtów.")

                        print(f"[*] Odbieranie danych obrazu...")
                        image_data = ser.read(image_size)

                        if len(image_data) == image_size:
                            timestamp = time.strftime("%Y%m%d-%H%M%S")
                            filename = f'otrzymany_obrazek_{timestamp}.jpg'
                            
                            with open(filename, 'wb') as f:
                                f.write(image_data)
                            
                            print(f"[+] SUKCES! Obraz został zapisany do pliku: {filename}")
                            print("\nOczekiwanie na kolejny obraz...")
                        else:
                            print(f"[!] BŁĄD: Otrzymano niekompletne dane! Odebrano {len(image_data)} z {image_size} bajtów.")

                    elif line:
                        print(f"[Arduino C]: {line}")

                except Exception as e:
                    # Ignorujemy pojedyncze błędy dekodowania, które mogą się zdarzyć
                    pass

    except serial.SerialException as e:
        print(f"BŁĄD KRYTYCZNY: Nie można otworzyć portu {SERIAL_PORT}.")
        print(f"Sprawdź połączenie i nazwę portu.")
    except KeyboardInterrupt:
        print("\nProgram zatrzymany przez użytkownika.")

if __name__ == '__main__':
    main()