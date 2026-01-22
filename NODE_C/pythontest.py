import serial
import time
import os
import io
import random  # <--- Potrzebne do losowania
from PIL import Image
import numpy as np

# --- Konfiguracja ---
SERIAL_PORT = 'COM11' 
BAUD_RATE = 115200
START_MARKER = 'START_IMAGE_TRANSFER'
RESULTS_DIR = "tests_home_cam"
os.makedirs(RESULTS_DIR, exist_ok=True)

# --- KONFIGURACJA SYMULACJI BŁĘDÓW ---
SIMULATE_LOSS = True       # Czy psuć obraz? (True = TAK, False = NIE)
LOSS_PERCENTAGE = 5        # Ile procent pakietów usunąć (np. 5%)
LORA_PAYLOAD_SIZE = 253    # Rozmiar pakietu przyjęty w symulacji
# --------------------------------------

SPARSE_BACKGROUND_COLOR = 0 

base_image_pil = None 
last_width = 0
last_height = 0
last_block_size = 0

def log(message):
    timestamp = time.strftime("%H:%M:%S")
    print(f"[{timestamp}] {message}")

# --- Funkcje symulujące uszkodzenia ---
def packetize_data(binary_data, packet_size):
    """Dzieli ciągły strumień bajtów na listę pakietów."""
    return [binary_data[i:i + packet_size] for i in range(0, len(binary_data), packet_size)]

def simulate_loss_on_buffer(binary_data, packet_size, loss_percentage):
    """
    Przyjmuje poprawne dane, dzieli je na pakiety i usuwa losowe fragmenty.
    Symuluje to brak mechanizmu retransmisji w LoRa.
    """
    packets = packetize_data(binary_data, packet_size)
    total_packets = len(packets)
    packets_to_lose = int(total_packets * (loss_percentage / 100.0))
    
    # Zabezpieczenie: staramy się nie usuwać pakietu nr 0 (nagłówka JPEG),
    # żeby biblioteka PIL w ogóle rozpoznała, że to obraz, a nie śmieci.
    if total_packets > 1:
        indices_to_drop = set(random.sample(range(1, total_packets), packets_to_lose))
    else:
        indices_to_drop = set()

    # Składanie z powrotem, pomijając wylosowane indeksy
    corrupted_buffer = io.BytesIO()
    for i, packet in enumerate(packets):
        if i not in indices_to_drop:
            corrupted_buffer.write(packet)
            
    log(f"[SIM] Symulacja: Usunięto {packets_to_lose} z {total_packets} pakietów ({loss_percentage}%).")
    return corrupted_buffer.getvalue()
# --------------------------------------

def apply_sparse_jpeg(base_img_pil, sparse_jpeg_data, background_color=0):
    if base_img_pil is None:
        log("[!] Błąd: Obraz bazowy nie istnieje.")
        return None

    try:
        log("[*] Dekodowanie Sparse JPEG...")
        sparse_stream = io.BytesIO(sparse_jpeg_data)
        
        # Przy uszkodzonym pliku tutaj często występuje błąd
        try:
            sparse_img_pil = Image.open(sparse_stream).convert('L')
            sparse_img_pil.load() # Wymuszenie wczytania danych
        except Exception as e:
            log(f"[!] Błąd struktury JPEG (oczekiwane przy symulacji błędów): {e}")
            return None

        if sparse_img_pil.size != base_img_pil.size:
            log(f"[!] Błąd wymiarów: {sparse_img_pil.size} != {base_img_pil.size}")
            return None

        base_arr = np.array(base_img_pil, dtype=np.uint8)
        sparse_arr = np.array(sparse_img_pil, dtype=np.uint8)

        changed_pixels_mask = sparse_arr != background_color
        base_arr[changed_pixels_mask] = sparse_arr[changed_pixels_mask]

        return Image.fromarray(base_arr, mode='L')

    except Exception as e:
        log(f"[!] Krytyczny błąd apply_sparse: {e}")
        return None

def main():
    global base_image_pil, last_width, last_height, last_block_size

    log(f"Odbiornik Start na {SERIAL_PORT}. Symulacja uszkodzeń: {SIMULATE_LOSS}")

    try:
        with serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=2) as ser:
            log("Połączono! Oczekiwanie...")
            
            while True:
                try:
                    line = ser.readline().decode('utf-8', errors='ignore').strip()
                    if not line: continue

                    if line == START_MARKER:
                        log(f"\n[+] Wykryto marker startu!")
                        
                        meta_line = ser.readline().decode('utf-8').strip()
                        parts = meta_line.split(',')
                        if len(parts) != 4: continue

                        expected_size = int(parts[0].strip())
                        w = int(parts[1].strip())
                        h = int(parts[2].strip())
                        blk = int(parts[3].strip())
                        
                        log(f"[*] Odbieranie {expected_size} bajtów (Czyste dane z C++)...")
                        original_data = ser.read(expected_size)

                        if len(original_data) != expected_size:
                            log("[!] Błąd: Niekompletny transfer UART.")
                            continue

                        # --- TU NASTĘPUJE ZMIANA DANYCH ---
                        if SIMULATE_LOSS:
                            # Nadpisujemy dane uszkodzoną wersją
                            data_to_process = simulate_loss_on_buffer(original_data, LORA_PAYLOAD_SIZE, LOSS_PERCENTAGE)
                        else:
                            data_to_process = original_data
                        # ----------------------------------

                        timestamp = time.strftime("%Y%m%d-%H%M%S")
                        filename_prefix = ""

                        # Logika przetwarzania (na potentially uszkodzonych danych)
                        is_first_frame = (base_image_pil is None)

                        if is_first_frame:
                            log("[*] Przetwarzanie Full Frame (może być uszkodzony)...")
                            try:
                                img_stream = io.BytesIO(data_to_process)
                                img_pil = Image.open(img_stream).convert('L')
                                img_pil.load() # Test integralności

                                base_image_pil = img_pil
                                last_width = w
                                last_height = h
                                last_block_size = blk
                                filename_prefix = f"base_{timestamp}_loss{LOSS_PERCENTAGE}"
                                log("[+] Obraz bazowy przyjęty.")
                            except Exception as e:
                                log(f"[!] Obraz bazowy zbyt uszkodzony: {e}")
                                filename_prefix = f"broken_base_{timestamp}"
                                # Nie zapisujemy base_image_pil, żeby spróbować następny
                        
                        else:
                            # Sparse Frame
                            log("[*] Przetwarzanie Sparse Frame...")
                            # Tu sprawdzamy wymiary na podstawie metadanych z UART (bo są pewne), 
                            # a nie z nagłówka JPEG (bo może być uszkodzony)
                            if w != last_width or h != last_height:
                                log("[!] Zmiana rozdzielczości - reset bazy.")
                                base_image_pil = None
                                continue
                            
                            updated = apply_sparse_jpeg(base_image_pil, data_to_process, SPARSE_BACKGROUND_COLOR)
                            if updated:
                                base_image_pil = updated
                                filename_prefix = f"updated_{timestamp}_loss{LOSS_PERCENTAGE}"
                            else:
                                log("[!] Nie udało się nałożyć łatki (zbyt duże uszkodzenia).")
                                filename_prefix = f"fail_diff_{timestamp}"

                        # Zapisywanie
                        # Zapisujemy WYNIKOWY (popsuty) plik binarny, żebyś mógł go potem otworzyć w hex edytorze
                        try:
                            corrupted_name = os.path.join(RESULTS_DIR, f"{filename_prefix}_corrupted_bytes.jpg")
                            with open(corrupted_name, 'wb') as f:
                                f.write(data_to_process)
                        except: pass

                        # Zapisujemy wyrenderowany obraz (jeśli się udało)
                        if base_image_pil:
                            render_name = os.path.join(RESULTS_DIR, f"{filename_prefix}_render.png")
                            try:
                                base_image_pil.save(render_name)
                                log(f"[+] Zapisano: {render_name}")
                            except: pass
                        
                        log("--- Koniec cyklu ---")

                    elif line:
                        log(f"[Node C]: {line}")

                except Exception as e:
                    log(f"Error loop: {e}")

    except Exception as e:
        log(f"CRITICAL: {e}")

if __name__ == '__main__':
    main()